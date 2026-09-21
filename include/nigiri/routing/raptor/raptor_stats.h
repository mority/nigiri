#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace nigiri::routing {

struct raptor_stats {
  std::map<std::string, std::uint64_t> to_map() const {
    return {
        {"n_routing_time", n_routing_time_},
        {"n_footpaths_visited", n_footpaths_visited_},
        {"n_routes_visited", n_routes_visited_},
        {"n_earliest_trip_calls", n_earliest_trip_calls_},
        {"n_earliest_arrival_updated_by_route",
         n_earliest_arrival_updated_by_route_},
        {"n_earliest_arrival_updated_by_footpath",
         n_earliest_arrival_updated_by_footpath_},
        {"fp_update_prevented_by_lower_bound",
         fp_update_prevented_by_lower_bound_},
        {"route_update_prevented_by_lower_bound",
         route_update_prevented_by_lower_bound_},
        {"n_td_offsets_evaluated", n_td_offsets_evaluated_},
        {"n_td_offsets_updated", n_td_offsets_updated_},
        {"n_td_footpaths_visited", n_td_footpaths_visited_},
    };
  }

  raptor_stats operator+(raptor_stats const& o) const {
    auto copy = *this;
    copy.n_routing_time_ += o.n_routing_time_;
    copy.n_footpaths_visited_ += o.n_footpaths_visited_;
    copy.n_routes_visited_ += o.n_routes_visited_;
    copy.n_earliest_trip_calls_ += o.n_earliest_trip_calls_;
    copy.n_earliest_arrival_updated_by_route_ +=
        o.n_earliest_arrival_updated_by_route_;
    copy.n_earliest_arrival_updated_by_footpath_ +=
        o.n_earliest_arrival_updated_by_footpath_;
    copy.fp_update_prevented_by_lower_bound_ +=
        o.fp_update_prevented_by_lower_bound_;
    copy.route_update_prevented_by_lower_bound_ +=
        o.route_update_prevented_by_lower_bound_;
    copy.n_td_offsets_evaluated_ += o.n_td_offsets_evaluated_;
    copy.n_td_offsets_updated_ += o.n_td_offsets_updated_;
    copy.n_td_footpaths_visited_ += o.n_td_footpaths_visited_;
    return copy;
  }

  std::uint64_t n_routing_time_{0ULL};
  std::uint64_t n_footpaths_visited_{0ULL};
  std::uint64_t n_routes_visited_{0ULL};
  std::uint64_t n_earliest_trip_calls_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_route_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_footpath_{0ULL};
  std::uint64_t fp_update_prevented_by_lower_bound_{0ULL};
  std::uint64_t route_update_prevented_by_lower_bound_{0ULL};

  // Time-dependent offsets on the last mile: how often the search read a
  // sequence, and how often that improved the arrival at the destination.
  // The GPU implementation does not maintain these.
  std::uint64_t n_td_offsets_evaluated_{0ULL};
  std::uint64_t n_td_offsets_updated_{0ULL};
  // Time-dependent footpaths relaxed in the transfer phase; a subset of
  // n_footpaths_visited_, which counts static and time-dependent alike.
  std::uint64_t n_td_footpaths_visited_{0ULL};
};

}  // namespace nigiri::routing
