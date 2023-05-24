#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_query_reached.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct queue {
  queue() = delete;
  explicit queue(reached& r, day_idx_t const base) : r_(r), base_(base) {}

  void reset();

  void enqueue(transport_idx_t const transport_idx,
               std::uint16_t const stop_idx,
               day_idx_t const transport_day,
               std::uint32_t const n_transfers,
               std::uint32_t const transferred_from);

  auto& operator[](unsigned pos) { return segments_[pos]; }

  reached& r_;
  day_idx_t const base_;
  std::vector<std::uint32_t> start_;
  std::vector<std::uint32_t> end_;
  std::vector<transport_segment> segments_;
};

}  // namespace nigiri::routing::tripbased
