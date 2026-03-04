#include "nigiri/routing/lb_hops.h"

#include "utl/enumerate.h"

#include "nigiri/for_each_meta.h"

namespace nigiri::routing {

template <direction SearchDir>
void lb_hops(timetable const& tt,
             query const& q,
             std::vector<location_idx_t>& queue,
             vector_map<location_idx_t, std::uint8_t>& location_hops) {
  auto const& adjacency = SearchDir == direction::kForward
                              ? tt.fwd_search_adjacency_[kDefaultProfile]
                              : tt.bwd_search_adjacency_[kDefaultProfile];
  queue.clear();
  queue.reserve(tt.n_locations());
  location_hops.resize(tt.n_locations());
  utl::fill(location_hops, std::numeric_limits<std::uint8_t>::max());

  // init
  for (auto const& o : q.destination_) {
    for_each_meta(tt, q.dest_match_mode_, o.target(),
                  [&](location_idx_t const l) { location_hops[l] = 0U; });
  }
  for (auto const& [l, tds] : q.td_dest_) {
    for (auto const& td : tds) {
      if (td.duration() != footpath::kMaxDuration &&
          td.duration() < q.max_travel_time_) {
        for_each_meta(tt, q.dest_match_mode_, l,
                      [&](location_idx_t const x) { location_hops[x] = 0U; });
        break;
      }
    }
  }

  // run
  for (auto const k :
       interval{0U, std::min(kMaxTransfers, q.max_transfers_) + 2U}) {
    auto no_updates = true;
    for (auto const [i, t] : utl::enumerate(location_hops)) {
      if (k != t) [[likely]] {
        continue;
      }
      for (auto const n : adjacency[location_idx_t{i}]) {
        if (k + 1U < location_hops[n]) {
          location_hops[n] = k + 1U;
          no_updates = false;
        }
      }
    }
    if (no_updates) {
      break;
    }
  }
}

template void lb_hops<direction::kForward>(
    timetable const&, query const&, vector_map<location_idx_t, std::uint8_t>&);
template void lb_hops<direction::kBackward>(
    timetable const&, query const&, vector_map<location_idx_t, std::uint8_t>&);

}  // namespace nigiri::routing