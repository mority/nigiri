#include "nigiri/bidir_route_lb_tp.h"
#include "nigiri/routing/bidir_route_lb_tp.h"

#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include <vector>

#include "utl/get_or_create.h"

namespace nigiri::routing {

template <direction SearchDir>
void init(timetable const& tt, query const& q, bidir_route_lb_tp_state& state) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const& offsets = kFwd ? q.start_ : q.destination_;
  auto const& td_offsets = kFwd ? q.td_start_ : q.td_dest_;
  auto const match_mode = kFwd ? q.start_match_mode_ : q.dest_match_mode_;
  auto& round_times = kFwd ? state.fwd_round_times_ : state.bwd_round_times_;
  auto& station_mark = kFwd ? state.fwd_station_mark_ : state.bwd_station_mark_;
  auto& reached = kFwd ? state.fwd_reached_ : state.bwd_reached_;

  state.min_.clear();
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m = utl::get_or_create(state.min_, root,
                                 [&] { return round_times[0][root]; });
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

  for (auto const& [l, t] : state.min_) {
    for_each_meta(tt, q.dest_match_mode_, l, [&](location_idx_t const meta) {
      state.round_times_[0][meta] =
          std::min(t, state.round_times_[0][meta]);  // necessary to min again?
      state.station_mark_.set(to_idx(meta), true);
    });
  }
}

void bidir_route_lb_tp(timetable const& tt,
                       query const& q,
                       bidir_route_lb_tp_state& state) {
  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_root_seq = tt.lb_route_root_seq_[q.prf_idx_];

  state.reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());

  // init (k = 0)
}
}  // namespace nigiri::routing