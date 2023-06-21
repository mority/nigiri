#pragma once

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/routing/tripbased/queue.h"
#include "nigiri/routing/tripbased/reached.h"
#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

// a route that reaches the destination
struct l_entry {
  l_entry(route_idx_t route_idx, std::uint16_t stop_idx, duration_t time)
      : route_idx_(route_idx), stop_idx_(stop_idx), time_(time) {}
  // the route index of the route that reaches the target location
  route_idx_t route_idx_;
  // the stop index at which the route reaches the target location
  std::uint16_t stop_idx_;
  // the time it takes after exiting the route until the target location is
  // reached
  duration_t time_;
};

struct tb_query_state {
  tb_query_state() = delete;
  tb_query_state(timetable const& tt,
                 transfer_set const& ts,
                 day_idx_t const base)
      : ts_{ts}, base_{base}, r_{tt}, q_{r_, base} {
    l_.reserve(128);
    t_min_.resize(kNumTransfersMax, unixtime_t::max());
    q_.start_.reserve(kNumTransfersMax);
    q_.end_.reserve(kNumTransfersMax);
    q_.segments_.reserve(10000);
    start_locations_.reserve(20);
    start_times_.reserve(20);
  }

  // transfer set built by preprocessor
  transfer_set const& ts_;

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

  // start locations
  std::vector<location_idx_t> start_locations_;

  // start times
  std::vector<unixtime_t> start_times_;
};

}  // namespace nigiri::routing::tripbased