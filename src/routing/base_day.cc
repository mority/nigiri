#include "nigiri/routing/base_day.h"

#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

day_idx_t base_day_idx(timetable const& tt, query const& q) {
  auto const search_interval = q.get_search_interval();
  return day_idx_t{
      std::chrono::duration_cast<date::days>(
          std::chrono::round<std::chrono::days>(
              search_interval.from_ +
              ((search_interval.to_ - search_interval.from_) / 2)) -
          tt.internal_interval().from_)
          .count()};
}

date::sys_days base_date(timetable const& tt, const day_idx_t base) {
  return tt.internal_interval_days().from_ +
         static_cast<int>(base.v_) * date::days{1};
}

}  // namespace nigiri::routing