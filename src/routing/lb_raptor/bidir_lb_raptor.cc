#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include <vector>

#include "utl/get_or_create.h"
#include "utl/pipes/remove_if.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "utl/enumerate.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

void bidir_lb_raptor::reset(unsigned const n_locations,
                            unsigned const n_lb_routes) {
  auto const reset_round_times = [&](auto& round_times) {
    for (auto& a : round_times) {
      a.resize(n_locations);
      utl::fill(a, kUnreachable);
    }
  };
  reset_round_times(fwd_round_times_);
  reset_round_times(bwd_round_times_);

  tmp_.resize(n_locations);
  utl::fill(tmp_, kUnreachable);

  auto const reset_bitvec = [&](auto& v, auto const size) {
    v.resize(size);
    utl::fill(v.blocks_, 0U);
  };

  reset_bitvec(fwd_station_mark_, n_locations);
  reset_bitvec(bwd_station_mark_, n_locations);
  reset_bitvec(prev_station_mark_, n_locations);
  reset_bitvec(fwd_reached_, n_locations);
  reset_bitvec(bwd_reached_, n_locations);
  reset_bitvec(is_start_, n_locations);
  reset_bitvec(is_dest_, n_locations);
  reset_bitvec(lb_route_mark_, n_lb_routes);

  meetpoints_.clear();
  meetpoints_.reserve(1000);

  patterns_.clear();
}

template <direction SearchDir>
void bidir_lb_raptor::init(timetable const& tt, query const& q) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const& offsets = kFwd ? q.start_ : q.destination_;
  auto const& td_offsets = kFwd ? q.td_start_ : q.td_dest_;
  auto const match_mode = kFwd ? q.start_match_mode_ : q.dest_match_mode_;
  auto& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;
  auto& reached = kFwd ? fwd_reached_ : bwd_reached_;

  min_.clear();
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m =
        utl::get_or_create(min_, root, [&] { return round_times[0][root]; });
    m = std::min(d, m);
  };

  for (auto const& o : offsets) {
    for_each_meta(tt, match_mode, o.target(), [&](location_idx_t const l) {
      update_min(l, static_cast<std::uint16_t>(o.duration().count()));
    });
  }

  for (auto const& [l, tds] : td_offsets) {
    for (auto const& td : tds) {
      if (td.duration() != footpath::kMaxDuration &&
          td.duration() < q.max_travel_time_) {
        update_min(l, static_cast<std::uint16_t>(td.duration().count()));
      }
    }
  }

  for (auto const& [l, t] : min_) {
    for_each_meta(tt, match_mode, l, [&](location_idx_t const meta) {
      round_times[0][meta] =
          std::min(t, round_times[0][meta]);  // necessary to min again?
      station_mark.set(to_idx(meta), true);
      reached.set(to_idx(meta), true);
      if constexpr (kFwd) {
        is_start_.set(to_idx(meta), true);
      } else {
        is_dest_.set(to_idx(meta), true);
      }
    });
  }
}

template <direction SearchDir>
bool bidir_lb_raptor::run(timetable const& tt,
                          query const& q,
                          unsigned const k) {
  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_root_seq = tt.lb_route_root_seq_[q.prf_idx_];

  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;
  auto& reached = kFwd ? fwd_reached_ : bwd_reached_;
  auto& rev_reached = kFwd ? bwd_reached_ : fwd_reached_;

  for (auto const l :
       interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
    round_times[k][l] = round_times[k - 1U][l];
  }
  utl::fill(tmp_, kUnreachable);

  auto any_marked = false;
  station_mark.for_each_set_bit([&](std::uint64_t const i) {
    for (auto const r : routes[location_idx_t{i}]) {
      any_marked = true;
      lb_route_mark_.set(to_idx(r), true);
    }
  });
  if (!any_marked) {
    return false;
  }

  std::swap(prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  any_marked = false;
  lb_route_mark_.for_each_set_bit([&](auto const i) {
    auto const r = lb_route_idx_t{i};
    auto const& segment_layovers = route_times[r];
    auto const& seq = route_root_seq[r];

    for (auto x = 0U; x != seq.size(); ++x) {
      auto const in = kFwd ? x : seq.size() - x - 1U;
      auto const l_in = seq[in];

      if (!prev_station_mark_.test(to_idx(l_in))) {
        continue;
      }

      auto lb = round_times[k - 1U][l_in];
      auto const step = kFwd ? 1 : -1;
      for (auto out = static_cast<std::int32_t>(in + step);
           0 <= out && out < static_cast<std::int32_t>(seq.size());
           out += step) {
        auto const l_out = seq[out];
        auto const segment =
            kFwd ? segment_layovers[(out - 1) * 2] : segment_layovers[out * 2];

        lb += segment.count();
        if (lb < std::min(round_times[k][l_out], tmp_[l_out])) {
          tmp_[l_out] = lb;
          station_mark.set(to_idx(l_out), true);
          reached.set(to_idx(l_out), true);
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
    return false;
  }

  utl::fill(lb_route_mark_.blocks_, 0U);

  std::swap(prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const time_after_transfer =
        tmp_[l] +
        adjusted_transfer_time(q.transfer_time_settings_,
                               tt.locations_.transfer_time_[l].count());
    round_times[k][l] = time_after_transfer;
    station_mark.set(i, true);
    reached.set(i, true);
  });

  prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const expand_fps = [&](auto const x) {
      for (auto const fp : kFwd ? tt.locations_.footpaths_out_[q.prf_idx_][x]
                                : tt.locations_.footpaths_in_[q.prf_idx_][x]) {
        auto const root = tt.locations_.get_root_idx(fp.target());
        auto const time_after_fp =
            tmp_[l] + adjusted_transfer_time(q.transfer_time_settings_,
                                             fp.duration().count());
        if (time_after_fp < round_times[k][root]) {
          round_times[k][root] = time_after_fp;
          station_mark.set(to_idx(root), true);
          reached.set(to_idx(root), true);
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
    return false;
  }

  station_mark.for_each_set_bit([&](auto const i) {
    if (rev_reached.test(i)) {
      station_mark.set(i, false);
      auto const l = location_idx_t{i};
      if (utl::find_if(meetpoints_, [&](auto const m) {
            return matches(tt, location_match_mode::kEquivalent, l, m);
          }) == end(meetpoints_)) {
        meetpoints_.push_back(l);
      }
    }
  });

  return station_mark.any();
}

void bidir_lb_raptor::execute(timetable const& tt,
                              query const& q,
                              unsigned const) {
  reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());

  // init (k = 0)
  init<direction::kForward>(tt, q);
  ;
  init<direction::kBackward>(tt, q);

  // run
  auto run_fwd = true;
  auto run_bwd = true;
  for (auto k = 1U; k != (std::min(q.max_transfers_, kMaxTransfers) + 2U) / 2U;
       ++k) {
    if (run_fwd) {
      run_fwd = run<direction::kForward>(tt, q, k);
      meetpoints_to_patterns<direction::kForward>(tt, q, k);
    }
    if (run_bwd) {
      run_bwd = run<direction::kBackward>(tt, q, k);
      meetpoints_to_patterns<direction::kBackward>(tt, q, k);
    }
  }
}

}  // namespace nigiri::routing