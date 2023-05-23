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
  route_idx_t const route_idx_{};
  // the stop index at which the route reaches the target location
  std::uint16_t const stop_idx_{};
  // the time it takes after exiting the route until the target location is
  // reached
  duration_t time_{};
};

struct tb_query_state {
  tb_query_state() = delete;
  tb_query_state(tb_preprocessing& tbp) : tbp_(tbp) {
    l_.reserve(128);
    // TODO reserve space for r_
    q_.start_.reserve(16);
    q_.end_.reserve(16);
    q_.segments_.reserve(10000);
  }

  void reset();

  // should contain a built transfer set
  tb_preprocessing& tbp_;

  // routes that reach the target stop
  std::vector<l_entry> l_;

  // reached stops per transport
  reached r_{tbp_};

  // queues of transport segments
  queue q_{r_};
};

}  // namespace nigiri::routing::tripbased