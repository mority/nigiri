#pragma once

#include "nigiri/routing/lb_raptor/bidir_lb_raptor_state.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

void bidir_lb_raptor(timetable const&,
                     query const&,
                     bidir_lb_raptor_state&,
                     unsigned n_meetpoints = 50U);

}  // namespace nigiri::routing