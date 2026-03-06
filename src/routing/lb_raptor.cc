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

  state.resize(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());
  state.clear();

  // init (k = 0)
  std::map<location_idx_t, std::uint16_t> min;
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m = utl::get_or_create(
        min, root, [&] { return state.location_round_lb_[root][0]; });
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
      state.location_round_lb_[meta].fill(std::min(
          t, state.location_round_lb_[meta][0]));  // necessary to min again?
      state.station_mark_.set(to_idx(meta), true);
    });
  }

  for (auto const& o : q.start_) {
    state.is_start_.set(to_idx(tt.locations_.get_root_idx(o.target())), true);
  }
 // run
  for (auto k = 1U; k != std::min(q.max_transfers_, kMaxTransfers) + 2U; ++k) {
    auto any_marked = false;
    state.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      for (auto const r : routes[location_idx_t{i}]) {
        any_marked = true;
        state.lb_route_mark_.set(to_idx(r), true);
      }
    });
    if (!any_marked) {
      break;
    }

    std::swap(state.prev_station_mark_, state.station_mark_);
    utl::fill(state.station_mark_.blocks_, 0U);

    any_marked = false;
    state.lb_route_mark_.for_each_set_bit([&](auto const i) {
      auto const r = lb_route_idx_t{i};
      auto const& segment_layovers = route_times[r];
      auto const& seq = route_root_seq[r];

      for (auto s = 1U; s != seq.size(); ++s) {
        auto const stop_idx = kFwd ? s : seq.size() - s - 1U;
        auto const l = seq[stop_idx];

        if (!state.prev_station_mark_.test(to_idx(l))) {
          continue;
        }

        auto lb = state.location_round_lb_[l][k - 1U];
        auto const step = kFwd ? -1 : 1;
        for (auto t = static_cast<std::int32_t>(stop_idx + step);
             0 <= t && t < static_cast<std::int32_t>(seq.size()); t += step) {
          auto const segment =
              kFwd ? segment_layovers[t * 2] : segment_layovers[(t - 1) * 2];

          lb += segment.count();
          if (lb < state.location_round_lb_[seq[t]][k]) {
            std::fill(begin(state.location_round_lb_[seq[t]]) + k,
                      end(state.location_round_lb_[seq[t]]), lb);
            state.station_mark_.set(to_idx(seq[t]), true);
            any_marked = true;

            auto const layover = segment_layovers[t * 2 - 1].count();
            lb += layover;
          } else {
            break;
          }
        }
      }
    });

    if (!any_marked) {
      break;
    }

    utl::fill(state.lb_route_mark_.blocks_, 0U);

    std::swap(state.prev_station_mark_, state.station_mark_);
    utl::fill(state.station_mark_.blocks_, 0U);

    state.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l = location_idx_t{i};

      auto const visit = [&](lb_neighbor const n) {
        auto const lb_pt = static_cast<std::uint16_t>(
            state.location_round_lb_[l][k - 1U] + n.pt_duration_);

        auto const lb_transfer = static_cast<std::uint16_t>(
            lb_pt + adjusted_transfer_time(q.transfer_time_settings_,
                                           n.transfer_duration_));
        if (lb_transfer < state.location_round_lb_[n.l_][k]) {
          std::fill(begin(state.location_round_lb_[n.l_]) + k,
                    end(state.location_round_lb_[n.l_]), lb_transfer);
          state.station_mark_.set(to_idx(n.l_), true);
          any_marked = true;
        }
      };

      for (auto const n : adjacency[l]) {
        visit(n);
      }
    });

    if (!any_marked) {
      break;
    }
  }

  // propagate lb to children
  // for (auto const l :
  //      interval{location_idx_t{0}, location_idx_t{tt.n_locations()}}) {
  //   for (auto const c : tt.locations_.children_[l]) {
  //     for (auto [plb, clb] :
  //          utl::zip(location_round_lb[l], location_round_lb[c])) {
  //       clb = std::min(plb, clb);
  //     }
  //   }
  // }
}

template void lb_raptor<direction::kForward>(timetable const&,
                                             query const&,
                                             lb_raptor_state&);

template void lb_raptor<direction::kBackward>(timetable const&,
                                              query const&,
                                              lb_raptor_state&);

}  // namespace nigiri::routing
