#pragma once

#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct footpath;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

static constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

struct lb_raptor_state {
  void reset(unsigned const n_locations, unsigned const n_lb_routes) {
    for (auto& a : round_times_) {
      a.resize(n_locations);
      utl::fill(a, kUnreachable);
    }

    tmp_.resize(n_locations);
    utl::fill(tmp_, kUnreachable);

    station_mark_.resize(n_locations);
    utl::fill(station_mark_.blocks_, 0U);

    prev_station_mark_.resize(n_locations);
    // will be zeroed after first swap

    lb_route_mark_.resize(n_lb_routes);
    utl::fill(lb_route_mark_.blocks_, 0U);
  }

  void zero_round_times() {
    for (auto& a : round_times_) {
      utl::fill(a, 0U);
    }
  }

  std::array<vector_map<location_idx_t, std::uint16_t>, kMaxTransfers + 2U>
      round_times_;
  vector_map<location_idx_t, std::uint16_t> tmp_;
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec lb_route_mark_;
};

template <direction SearchDir>
void lb_raptor(timetable const&, query const&, lb_raptor_state&);

std::array<vector_map<location_idx_t, std::uint16_t>, kMaxTransfers + 2U>
get_zero_lb(unsigned n_locations);

}  // namespace nigiri::routing