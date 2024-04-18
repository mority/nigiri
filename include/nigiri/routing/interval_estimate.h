#pragma once

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;

template <direction SearchDir>
struct interval_estimator {
  explicit interval_estimator(timetable const& tt, query const& q)
      : tt_{tt}, q_{q} {}

  void initial_estimate(interval<unixtime_t>& itv) const;

  void estimate(interval<unixtime_t>& itv, std::uint32_t num_con_req) const;

private:
  std::uint32_t events_in_itv(interval<unixtime_t> const& itv) const;

  timetable const& tt_;
  query const& q_;
};

}  // namespace nigiri::routing