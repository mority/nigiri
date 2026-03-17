#pragma once

#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

static constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

struct bidir_lb_raptor_state {
  void reset(unsigned const n_locations, unsigned const n_lb_routes) {
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
    reset_bitvec(lb_route_mark_, n_lb_routes);

    tps_.clear();
  }

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
  vector<std::array<location_idx_t, kMaxTransfers>> tps_;
  std::map<location_idx_t, std::uint16_t> min_;
};

std::vector<std::vector<location_idx_t>> bidir_lb_raptor(timetable const&,
                                                         query const&);

}  // namespace nigiri::routing