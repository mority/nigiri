#pragma once

#include "nigiri/routing/query.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

void ping_pong(timetable const&, rt_timetable const*, query const&);

}  // namespace nigiri::routing