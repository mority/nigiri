#pragma once

#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

struct bidir_lb_raptor {
  void execute(timetable const&, query const&, unsigned n_meetpoints = 50U);

private:
  void reset(unsigned n_locations, unsigned n_lb_routes);

  template <direction SearchDir>
  void init(timetable const&, query const&);

  template <direction SearchDir>
  bool run(timetable const&, query const&, unsigned k);

  template <direction SearchDir>
  void meetpoints_to_patterns(timetable const&, query const&, unsigned k);

  template <direction SearchDir>
  void reconstruct(timetable const&,
                   query const&,
                   location_idx_t,
                   unsigned k_start);

public:
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
  bitvec is_start_;
  bitvec is_dest_;
  bitvec lb_route_mark_;
  std::map<location_idx_t, std::uint16_t> min_;
  std::vector<location_idx_t> meetpoints_;
  std::vector<location_idx_t> current_pattern_;
  std::set<std::array<location_idx_t, kMaxTransfers>> patterns_;
};

}  // namespace nigiri::routing