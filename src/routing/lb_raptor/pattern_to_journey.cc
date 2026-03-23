#include "nigiri/routing/lb_raptor/pattern_to_journey.h"

#include "nigiri/timetable.h"

// #define trace(...)
#define trace fmt::println

namespace nigiri::routing {

template <direction SearchDir>
journey pattern_to_journey(timetable const& tt,
                           query const&,
                           std::vector<location_idx_t> const& pattern) {
  for (auto const [a, b] : utl::pairwise(pattern)) {
    trace("[pattern_to_journey] station pair: {} -> {}",
          tt.get_default_name(a), tt.get_default_name(b));
  }

  return journey{};
}

template journey pattern_to_journey<direction::kForward>(
    timetable const&, query const&, std::vector<location_idx_t> const&);
template journey pattern_to_journey<direction::kBackward>(
    timetable const&, query const&, std::vector<location_idx_t> const&);

}  // namespace nigiri::routing