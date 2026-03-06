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
  void resize(unsigned const n_locations, unsigned const n_lb_routes) {
    location_round_lb_.resize(n_locations);
    station_mark_.resize(n_locations);
    prev_station_mark_.resize(n_locations);
    is_start_.resize(n_locations);
    lb_route_mark_.resize(n_lb_routes);
  }

  void clear() {
    static constexpr auto kRoundLbInit = []() {
      auto ret = std::array<std::uint16_t, kMaxTransfers + 2>{};
      ret.fill(kUnreachable);
      return ret;
    }();
    utl::fill(location_round_lb_, kRoundLbInit);
    utl::fill(station_mark_.blocks_, 0U);
    utl::fill(is_start_.blocks_, 0U);
    utl::fill(lb_route_mark_.blocks_, 0U);
  }

  void zeroize() {
    static constexpr auto kLbZero = [] {
      auto a = std::array<std::uint16_t, kMaxTransfers + 2U>{};
      a.fill(0U);
      return a;
    }();
    utl::fill(location_round_lb_, kLbZero);
  }

  vector_map<location_idx_t, std::array<std::uint16_t, kMaxTransfers + 2U>>
      location_round_lb_;
  vector_map<location_idx_t,std:> tmp_
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec is_start_;
  bitvec lb_route_mark_;
};

template <direction SearchDir>
void lb_raptor(timetable const&, query const&, lb_raptor_state&);

}  // namespace nigiri::routing