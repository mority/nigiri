#pragma once

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;

day_idx_t base_day_idx(timetable const&, query const&);

date::sys_days base_date(timetable const&, const day_idx_t);

}  // namespace nigiri::routing