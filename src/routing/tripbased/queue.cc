#include "nigiri/routing/tripbased/queue.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void queue::reset() {
  start_.clear();
  start_.emplace_back(0U);
  end_.clear();
  end_.emplace_back(0U);
  segments_.clear();
}

void queue::enqueue(day_idx_t const transport_day,
                    transport_idx_t const transport_idx,
                    std::uint16_t const stop_idx,
                    std::uint16_t const n_transfers,
                    std::uint32_t const transferred_from) {
  // compute transport segment index
  auto const transport_segment_idx =
      embed_day_offset(base_, transport_day, transport_idx);

  // look-up the earliest stop index reached
  auto const r_query_res = r_.query(transport_segment_idx, n_transfers);
  if (stop_idx < r_query_res) {

    // new n?
    if (n_transfers == start_.size()) {
      start_.emplace_back(segments_.size());
      end_.emplace_back(segments_.size());
    }

    // add transport segment
    segments_.emplace_back(transport_segment_idx, stop_idx, r_query_res,
                           transferred_from);

    // increment index
    ++end_[n_transfers];

    // update reached
    r_.update(transport_segment_idx, stop_idx, n_transfers);
  }
}