#pragma once

#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"

namespace nigiri::routing {
struct journey;
struct bidir_lb_raptor_state;

std::optional<journey> guess_journey(timetable const&,
                    query const&,
                    bidir_lb_raptor_state const&, location_idx_t meetpoint);

}  // namespace nigiri::routing