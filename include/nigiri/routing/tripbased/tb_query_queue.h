#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_query_reached.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct transport_segment {
  transport_segment(transport_idx_t const transport_idx,
                    std::uint16_t const stop_idx_start,
                    std::uint16_t const stop_idx_end,
                    day_idx_t const day_idx,
                    std::uint32_t const transferred_from)
      : transport_idx_(transport_idx),
        stop_idx_start_(stop_idx_start),
        stop_idx_end_(stop_idx_end),
        day_idx_(day_idx.v_),
        transferred_from_(transferred_from) {}

  std::uint16_t get_stop_idx_start() const {
    return static_cast<std::uint16_t>(stop_idx_start_);
  }

  std::uint16_t get_stop_idx_end() const {
    return static_cast<std::uint16_t>(stop_idx_end_);
  }

  day_idx_t get_day_idx() const { return day_idx_t{day_idx_}; }

  transport_idx_t const transport_idx_{};

  std::uint32_t const stop_idx_start_ : STOP_IDX_BITS;
  std::uint32_t stop_idx_end_ : STOP_IDX_BITS;
  std::uint32_t const day_idx_ : DAY_IDX_BITS;

  // from which segment we transferred to this segment
  std::uint32_t const transferred_from_;
};

struct queue {
  queue() = delete;
  explicit queue(reached& r) : r_(r) {}

  void enqueue(transport_idx_t const transport_idx,
               std::uint16_t const stop_idx,
               day_idx_t const day_idx,
               std::uint32_t const n,
               std::uint32_t const transferred_from);

  reached& r_;

  // q_cur_[n] = cursor of Q_n
  std::vector<std::uint32_t> q_cur_;
  // q_start_[n] = start of Q_n
  std::vector<std::uint32_t> q_start_;
  // q_end_[n] = end of Q_n (exclusive)
  std::vector<std::uint32_t> q_end_;
  // all Q_n back to back
  std::vector<transport_segment> q_;
};

}  // namespace nigiri::routing::tripbased
