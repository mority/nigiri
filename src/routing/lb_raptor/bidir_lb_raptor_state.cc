#include "nigiri/routing/lb_raptor/bidir_lb_raptor_state.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

void bidir_lb_raptor_state::reset(unsigned const n_locations,
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
  reset_bitvec(lb_route_mark_, n_lb_routes);

  meetpoints_.clear();
}

}  // namespace nigiri::routing
