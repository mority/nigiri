#pragma once

#include "nigiri/types.h"

#include "nigiri/routing/pareto_set.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;
struct journey;

template <direction SearchDir>
struct interval_estimator {
  explicit interval_estimator(timetable const& tt, query const& q)
      : tt_{tt}, q_{q} {}

  interval<unixtime_t> initial(interval<unixtime_t> const&) const;

  interval<unixtime_t> extension(interval<unixtime_t> const&,
                                 pareto_set<journey> const&,
                                 std::uint32_t const num_con_req) const;

private:
  bool can_extend(interval<unixtime_t> const&) const;
  bool can_extend_both_dir(interval<unixtime_t> const&) const;
  std::uint32_t num_events(interval<unixtime_t> const&,
                           location_idx_t const,
                           std::vector<route_idx_t> const&) const;
  std::uint32_t num_start_events(interval<unixtime_t> const&) const;
  i32_minutes ext_from_journeys(interval<unixtime_t> const&,
                                pareto_set<journey> const&,
                                std::uint32_t const) const;

  timetable const& tt_;
  query const& q_;
};

}  // namespace nigiri::routing