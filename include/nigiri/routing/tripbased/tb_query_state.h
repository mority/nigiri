#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/routing/tripbased/tb_query_queue.h"
#include "nigiri/routing/tripbased/tb_query_reached.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

// a route that reaches the destination
struct l_entry {
  // the route index of the route that reaches the target location
  route_idx_t route_idx_{};
  // the stop index at which the route reaches the target location
  std::uint16_t stop_idx_{};
  // the time it takes after exiting the route until the target location is
  // reached
  duration_t time_{};
};

struct tb_query_state {
  tb_query_state() = delete;
  tb_query_state(tb_preprocessing& tbp, day_idx_t const base)
      : tbp_{tbp}, base_{base}, r_{tbp}, q_{r_, base} {
    l_.reserve(128);
    t_min_.resize(kNumTransfersMax, unixtime_t::max());
    q_.start_.reserve(kNumTransfersMax);
    q_.end_.reserve(kNumTransfersMax);
    q_.segments_.reserve(10000);
  }

  // should contain a built transfer set
  tb_preprocessing& tbp_;

  // base day of the query
  day_idx_t const base_;

  // routes that reach the target stop
  std::vector<l_entry> l_;

  // reached stops per transport
  reached r_;

  // minimum arrival times per number of transfers
  std::vector<unixtime_t> t_min_;

  // queues of transport segments
  queue q_;

  location_idx_t start_location_;
  unixtime_t start_time_;
};

}  // namespace nigiri::routing::tripbased