#pragma once

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/routing/tripbased/reached.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

using queue_idx_t = std::uint32_t;

struct q_n {
  q_n() = delete;
  explicit q_n(reached& r, day_idx_t const base) : r_(r), base_(base) {}

  void reset();

  void enqueue(day_idx_t const transport_day,
               transport_idx_t const,
               std::uint16_t const stop_idx,
               std::uint16_t const n_transfers,
               std::uint32_t const transferred_from);

  auto& operator[](queue_idx_t pos) { return segments_[pos]; }

  auto size() const { return segments_.size(); }

  void print(std::ostream&, queue_idx_t const);

  reached& r_;
  day_idx_t const base_;
  std::vector<queue_idx_t> start_;
  std::vector<queue_idx_t> end_;
  std::vector<transport_segment> segments_;
};

}  // namespace nigiri::routing::tripbased
