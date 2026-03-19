#include "nigiri/routing/bidir_lb_raptor/guess_journey.h"

#include "nigiri/routing/bidir_lb_raptor/bidir_lb_raptor.h"
#include "nigiri/routing/journey.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

auto get_rounds(bidir_lb_raptor_state const& state,
                location_idx_t const meetpoint) {
  auto const get_round = [&](auto const& round_times) {
    return utl::find_if(round_times,
                        [&](auto const& times) {
                          return times[meetpoint] != kUnreachable;
                        }) -
           begin(round_times);
  };
  return std::tuple{get_round(state.fwd_round_times_),
                    get_round(state.bwd_round_times_)};
}

std::optional<journey> guess_journey(timetable const&,
                                     query const&,
                                     bidir_lb_raptor_state const&,
                                     location_idx_t const) {
  // auto const [fwd_round, bwd_round] = get_rounds(state, meetpoint);

  return std::nullopt;
}

}  // namespace nigiri::routing