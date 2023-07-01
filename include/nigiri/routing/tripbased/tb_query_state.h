#pragma once

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/routing/tripbased/q_n.h"
#include "nigiri/routing/tripbased/reached.h"
#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

// a route that reaches the destination
struct route_dest {
  route_dest(route_idx_t route_idx, std::uint16_t stop_idx, std::uint16_t time)
      : route_idx_(route_idx), stop_idx_(stop_idx), time_(time) {}
  // the route index of the route that reaches the target location
  route_idx_t route_idx_;
  // the stop index at which the route reaches the target location
  std::uint16_t stop_idx_;
  // the time in it takes after exiting the route until the target location is
  // reached
  std::uint16_t time_;
};

struct query_start {
  query_start(location_idx_t const l, unixtime_t const t)
      : location_(l), time_(t) {}

  location_idx_t location_;
  unixtime_t time_;
};

struct tb_query_state {
  tb_query_state() = delete;
  tb_query_state(timetable const& tt,
                 transfer_set const& ts,
                 day_idx_t const base)
      : ts_{ts}, base_{base}, r_{tt}, q_{r_, base} {
    route_dest_.reserve(128);
    t_min_.resize(kNumTransfersMax, unixtime_t::max());
    q_.start_.reserve(kNumTransfersMax);
    q_.end_.reserve(kNumTransfersMax);
    q_.segments_.reserve(10000);
    query_starts_.reserve(20);
  }

  void new_query_reset() {
    route_dest_.clear();
    std::fill(t_min_.begin(), t_min_.end(), unixtime_t::max());
    r_.reset();
  }

  // transfer set built by preprocessor
  transfer_set const& ts_;

  // base day of the query
  day_idx_t const base_;

  // routes that reach the target stop
  std::vector<route_dest> route_dest_;

  // reached stops per transport
  reached r_;

  // minimum arrival times per number of transfers
  std::vector<unixtime_t> t_min_;

  // queues of transport segments
  q_n q_;

  std::vector<query_start> query_starts_;
};

}  // namespace nigiri::routing::tripbased