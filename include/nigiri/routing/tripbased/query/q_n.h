#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/types.h"
#include "reached.h"

namespace nigiri::routing::tripbased {

using queue_idx_t = std::uint32_t;

struct q_n {
  q_n() = delete;
  explicit q_n(reached& r) : r_(r) {}

  void reset(day_idx_t new_base);

  void enqueue(std::uint16_t const transport_day,
               transport_idx_t const,
               std::uint16_t const stop_idx,
               std::uint16_t const n_transfers,
               std::uint32_t const transferred_from);

  auto& operator[](queue_idx_t pos) { return segments_[pos]; }

  auto size() const { return segments_.size(); }

  void print(std::ostream&, queue_idx_t const);

  reached& r_;
  std::optional<day_idx_t> base_ = std::nullopt;
  std::vector<queue_idx_t> start_;
  std::vector<queue_idx_t> end_;
  std::vector<transport_segment> segments_;
};

}  // namespace nigiri::routing::tripbased