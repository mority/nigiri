#pragma once

#include "nigiri/routing/query.h"

namespace nigiri::routing {
struct journey;
struct bidir_lb_raptor_state;

std::vector<location_idx_t> meetpoint_to_pattern(timetable const&,
                                                 query const&,
                                                 bidir_lb_raptor_state const&,
                                                 location_idx_t meetpoint,
                                                 unsigned k,
                                                 direction dir);

}  // namespace nigiri::routing