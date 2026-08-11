#pragma once

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;
struct search_state;
struct raptor_state;
struct journey;

// Sched selects which raptor_state labels to reconstruct from:
// false (default) -- round_times_ (today's single-slot behavior).
// true  -- round_times_sched_ (rt_mode::both's scheduled slot). Callers must
//          also pass rtt == nullptr in this case, so that trip selection
//          never reads realtime data (matches the scheduled slot's
//          static-only semantics).
template <direction SearchDir, bool Sched = false>
void reconstruct_journey(timetable const&,
                         rt_timetable const*,
                         query const&,
                         raptor_state const&,
                         journey&,
                         date::sys_days const base,
                         day_idx_t const base_day_idx);

void optimize_footpaths(timetable const&,
                        rt_timetable const*,
                        query const&,
                        journey&);

void specify_td_offsets(query const&, journey&);

}  // namespace nigiri::routing
