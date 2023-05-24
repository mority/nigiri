#include "nigiri/routing/tripbased/tb_query_queue.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void queue::reset() {
  start_.clear();
  start_.emplace_back(0U);
  end_.clear();
  end_.emplace_back(0U);
  segments_.clear();
}

void queue::enqueue(transport_idx_t const transport_idx,
                    std::uint16_t const stop_idx,
                    day_idx_t const transport_day,
                    std::uint32_t const n_transfers,
                    std::uint32_t const transferred_from) {
  // look-up the earliest stop index reached
  auto const r_query_res = r_.query(transport_idx, day_idx);
  if (stop_idx < r_query_res) {

    // new n?
    if (n == start_.size()) {
      start_.emplace_back(segments_.size());
      end_.emplace_back(segments_.size());
    }

    // add transport segment
    segments_.emplace_back(transport_idx, stop_idx, r_query_res, day_idx,
                           transferred_from);
    ++end_[n];

    // update all transports of this route
    auto const route_idx = r_.tbp_.tt_.transport_route_[transport_idx];
    for (auto const transport_idx_it :
         r_.tbp_.tt_.route_transport_ranges_[route_idx]) {
      // construct d_new
      auto day_idx_new = day_idx;

      // set the bit of the day of the instance to false if the current
      // transport is earlier than the newly enqueued
      auto const mam_u =
          r_.tbp_.tt_.event_mam(transport_idx_it, 0U, event_type::kDep);
      auto const mam_t =
          r_.tbp_.tt_.event_mam(transport_idx, 0U, event_type::kDep);
      if (mam_u < mam_t) {
        ++day_idx_new;
      }

      r_.update(transport_idx_it, stop_idx, day_idx_new);
    }
  }
}