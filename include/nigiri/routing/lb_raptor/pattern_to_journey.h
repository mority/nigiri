#pragma once

#include "nigiri/routing/journey.h"

namespace nigiri::routing {

template <direction SearchDir>
journey pattern_to_journey(timetable const&,
                           query const&,
                           std::vector<location_idx_t> const&);

}  // namespace nigiri::routing