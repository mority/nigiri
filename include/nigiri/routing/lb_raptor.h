#pragma once

#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct footpath;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

struct lb_raptor_state {
  void reset(unsigned n_locations, unsigned n_lb_routes);

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