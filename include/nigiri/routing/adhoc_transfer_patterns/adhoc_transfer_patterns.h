#pragma once

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

void journeys_from_patterns(timetable const& tt,
                            pareto_set<journey>& results,
                            unixtime_t start_time);

}  // namespace nigiri::routing