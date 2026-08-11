#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct journey;

struct journey_validation_result {
  bool feasible_{true};
  std::optional<std::size_t> broken_leg_idx_{};
  std::string reason_{};
  // The journey's actual (realtime-adjusted) arrival time if feasible_;
  // otherwise the last realtime-confirmed time before the break (i.e. how
  // far the passenger would actually get).
  unixtime_t arrival_time_{};
};

journey_validation_result validate_journey(timetable const&,
                                           rt_timetable const&,
                                           journey const&);

}  // namespace nigiri::routing
