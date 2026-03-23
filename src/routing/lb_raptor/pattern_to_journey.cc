#include "nigiri/routing/lb_raptor/pattern_to_journey.h"

// #define trace(...)
#define trace fmt::println

namespace nigiri::routing {

template <direction SearchDir>
journey pattern_to_journey(timetable const&,
                           query const&,
                           std::vector<location_idx_t> const& pattern) {
  for (auto const [a, b] : utl::pairwise(pattern)) {
    trace("station pair: {} -> {}", a, b);
  }
}

template pattern_to_journey<direction::kForward>(
    timetable const&, query const&, std::vector<location_idx_t> const&);
template pattern_to_journey<direction::kBackward>(
    timetable const&, query const&, std::vector<location_idx_t> const&);

}  // namespace nigiri::routing