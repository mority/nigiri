#pragma once

#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

struct bidir_lb_raptor_state {
  void reset(unsigned n_locations, unsigned n_lb_routes);

  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      fwd_round_times_;
  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      bwd_round_times_;
  vector_map<location_idx_t, std::uint16_t> tmp_;
  bitvec fwd_station_mark_;
  bitvec bwd_station_mark_;
  bitvec prev_station_mark_;
  bitvec fwd_reached_;
  bitvec bwd_reached_;
  bitvec lb_route_mark_;
  std::map<location_idx_t, std::uint16_t> min_;
  vector<location_idx_t> meetpoints_;
};

void bidir_lb_raptor(timetable const&, query const&, bidir_lb_raptor_state&);

}  // namespace nigiri::routing