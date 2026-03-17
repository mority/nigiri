#include "nigiri/routing/lb_raptor.h"

#include "utl/get_or_create.h"
#include "utl/timing.h"
#include "utl/zip.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

#include "utl/enumerate.h"

namespace nigiri::routing {

template <direction SearchDir>
void lb_raptor(timetable const& tt, query const& q, lb_raptor_state& state) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_root_seq = tt.lb_route_root_seq_[q.prf_idx_];

  state.reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());

  // init (k = 0)
  std::map<location_idx_t, std::uint16_t> min;
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m = utl::get_or_create(min, root,
                                 [&] { return state.round_times_[0][root]; });
    m = std::min(d, m);
  };

  for (auto const& o : q.destination_) {
    for_each_meta(
        tt, q.dest_match_mode_, o.target(), [&](location_idx_t const l) {
          update_min(l, static_cast<std::uint16_t>(o.duration().count()));
        });
  }

  for (auto const& [l, tds] : q.td_dest_) {
    for (auto const& td : tds) {
      if (td.duration() != footpath::kMaxDuration &&
          td.duration() < q.max_travel_time_) {
        update_min(l, static_cast<std::uint16_t>(td.duration().count()));
      }
    }
  }

  for (auto const& [l, t] : min) {
    for_each_meta(tt, q.dest_match_mode_, l, [&](location_idx_t const meta) {
      state.round_times_[0][meta] =
          std::min(t, state.round_times_[0][meta]);  // necessary to min again?
      state.station_mark_.set(to_idx(meta), true);
    });
  }

  // run
  auto early_termination = false;
  for (auto k = 1U; k != std::min(q.max_transfers_, kMaxTransfers) + 2U; ++k) {
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      state.round_times_[k][l] = state.round_times_[k - 1U][l];
    }

    if (early_termination) {
      continue;
    }

    auto any_marked = false;
    state.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      for (auto const r : routes[location_idx_t{i}]) {
        any_marked = true;
        state.lb_route_mark_.set(to_idx(r), true);
      }
    });
    if (!any_marked) {
      early_termination = true;
      continue;
    }

    std::swap(state.prev_station_mark_, state.station_mark_);
    utl::fill(state.station_mark_.blocks_, 0U);

    any_marked = false;
    state.lb_route_mark_.for_each_set_bit([&](auto const i) {
      auto const r = lb_route_idx_t{i};
      auto const& segment_layovers = route_times[r];
      auto const& seq = route_root_seq[r];

      for (auto x = 1U; x != seq.size(); ++x) {
        auto const in = kFwd ? x : seq.size() - x - 1U;
        auto const l_in = seq[in];

        if (!state.prev_station_mark_.test(to_idx(l_in))) {
          continue;
        }

        auto lb = state.round_times_[k - 1U][l_in];
        auto const step = kFwd ? -1 : 1;
        for (auto out = static_cast<std::int32_t>(in + step);
             0 <= out && out < static_cast<std::int32_t>(seq.size());
             out += step) {
          auto const l_out = seq[out];
          auto const segment = kFwd ? segment_layovers[out * 2]
                                    : segment_layovers[(out - 1) * 2];

          lb += segment.count();
          if (lb < std::min(state.round_times_[k][l_out], state.tmp_[l_out])) {
            state.tmp_[l_out] = lb;
            state.station_mark_.set(to_idx(l_out), true);
            any_marked = true;

            if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
              auto const layover = segment_layovers[out * 2 - 1].count();
              lb += layover;
            } else {
              break;
            }
          } else {
            break;
          }
        }
      }
    });

    if (!any_marked) {
      early_termination = true;
      continue;
    }

    utl::fill(state.lb_route_mark_.blocks_, 0U);

    std::swap(state.prev_station_mark_, state.station_mark_);
    utl::fill(state.station_mark_.blocks_, 0U);

    state.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l = location_idx_t{i};

      auto const time_after_transfer =
          state.tmp_[l] +
          adjusted_transfer_time(q.transfer_time_settings_,
                                 tt.locations_.transfer_time_[l].count());
      state.round_times_[k][l] = time_after_transfer;
      state.station_mark_.set(i, true);
    });

    state.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l = location_idx_t{i};

      auto const expand_fps = [&](auto const x) {
        for (auto const fp :
             kFwd ? tt.locations_.footpaths_in_[q.prf_idx_][x]
                  : tt.locations_.footpaths_out_[q.prf_idx_][x]) {
          auto const root = tt.locations_.get_root_idx(fp.target());
          auto const time_after_fp =
              state.tmp_[l] + adjusted_transfer_time(q.transfer_time_settings_,
                                                     fp.duration().count());
          if (time_after_fp < state.round_times_[k][root]) {
            state.round_times_[k][root] = time_after_fp;
            state.station_mark_.set(to_idx(root), true);
          }
        }
      };

      expand_fps(l);
      for (auto const c : tt.locations_.children_[l]) {
        expand_fps(c);
        for (auto const cc : tt.locations_.children_[c]) {
          expand_fps(cc);
        }
      }
    });

    if (!any_marked) {
      early_termination = true;
    }
  }

  // propagate round times to children
  for (auto& times : state.round_times_) {
    for (auto const l :
         interval{location_idx_t{0}, location_idx_t{tt.n_locations()}}) {
      for (auto const c : tt.locations_.children_[l]) {
        times[c] = times[l];
        for (auto const cc : tt.locations_.children_[c]) {
          times[cc] = times[c];
        }
      }
    }
  }
}

template void lb_raptor<direction::kForward>(timetable const&,
                                             query const&,
                                             lb_raptor_state&);

template void lb_raptor<direction::kBackward>(timetable const&,
                                              query const&,
                                              lb_raptor_state&);

std::array<vector_map<location_idx_t, std::uint16_t>, kMaxTransfers + 2U>
get_zero_lb(unsigned const n_locations) {
  auto a = std::array<vector_map<location_idx_t, std::uint16_t>,
                      kMaxTransfers + 2U>{};
  for (auto& v : a) {
    v.resize(n_locations);
    utl::fill(v, 0U);
  }
  return a;
}

}  // namespace nigiri::routing
