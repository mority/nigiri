#include "nigiri/routing/tripbased/q_n.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/transport_segment.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void q_n::reset(day_idx_t new_base) {
#ifndef NDEBUG
  TBDL << "Resetting transport segment queue\n";
#endif
  base_ = new_base;
  start_.clear();
  start_.emplace_back(0U);
  end_.clear();
  end_.emplace_back(0U);
  segments_.clear();
}

void q_n::enqueue(std::uint16_t const transport_day,
                  transport_idx_t const transport_idx,
                  std::uint16_t const stop_idx,
                  std::uint16_t const n_transfers,
                  std::uint32_t const transferred_from) {
  assert(segments_.size() < std::numeric_limits<queue_idx_t>::max());
  assert(base_.has_value());

  // compute transport segment index
  auto const transport_segment_idx =
      embed_day_offset(base_.value().v_, transport_day, transport_idx);

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
#ifndef NDEBUG
    TBDL << "Enqueued transport segment: ";
    print(std::cout, static_cast<queue_idx_t>(segments_.size() - 1));
#endif

    // increment index
    ++end_[n_transfers];

    // update reached
    r_.update(transport_segment_idx, stop_idx, n_transfers);
  }
}

void q_n::print(std::ostream& out, queue_idx_t const q_idx) {
  out << "q_idx: " << std::to_string(q_idx) << ", segment of ";
  segments_[q_idx].print(out, r_.tt_);
}