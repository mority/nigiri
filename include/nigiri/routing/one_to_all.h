#pragma once

#include <limits>

#include "nigiri/common/delta_t.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

// Day of the search base (delta_t origin) for searches starting at start_time.
day_idx_t make_base(timetable const&, unixtime_t start_time);

struct fastest_offset {
  delta_t duration_{std::numeric_limits<delta_t>::max()};
  // k == 0: No connection
  // k == 1: Direct connection
  // k == 2: Connection with 1 transfer
  std::uint8_t k_{std::numeric_limits<std::uint8_t>::max()};
};

template <direction SearchDir>
raptor_state one_to_all(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q);

fastest_offset get_fastest_one_to_all_offsets(timetable const& tt,
                                              raptor_state const& state,
                                              direction,
                                              location_idx_t,
                                              unixtime_t start_time,
                                              std::uint8_t transfers);

void for_each_one_to_all_round_time(
    timetable const&,
    raptor_state const&,
    direction,
    location_idx_t,
    unixtime_t start_time,
    std::uint8_t transfers,
    std::function<void(std::uint8_t, duration_t)> const&);

}  // namespace nigiri::routing
