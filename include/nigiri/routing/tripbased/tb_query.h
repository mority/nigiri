#pragma once

#include <cinttypes>

#include "nigiri/routing/query.h"
#include "nigiri/routing/routing_time.h"
#include "tb_preprocessing.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

struct tb_query {
  tb_query() = delete;
  explicit tb_query(tb_preprocessing& tbp) : tbp_(tbp) {}

  tb_preprocessing& tbp_;

  // R(t) data structure - BEGIN
  struct r_entry {
    r_entry(transport_idx_t const transport_idx,
            unsigned const stop_idx,
            bitfield_idx_t bitfield_idx)
        : transport_idx_(transport_idx),
          stop_idx_(stop_idx),
          bitfield_idx_(bitfield_idx) {}

    transport_idx_t const transport_idx_;
    unsigned const stop_idx_;
    bitfield_idx_t const bitfield_idx_;
  };

  void r_update(transport_idx_t const,
                unsigned const stop_idx,
                bitfield const&);

  unsigned r_query(transport_idx_t const, bitfield const&);

  std::vector<r_entry> r_;
  // R(t) data structure - END

  // Q_n data structure - BEGIN
  struct transport_segment {
    transport_segment(transport_idx_t const transport_idx,
                      unsigned const stop_idx_start,
                      unsigned const stop_idx_end,
                      bitfield_idx_t const bitfield_idx)
        : transport_idx_(transport_idx),
          stop_idx_start_(stop_idx_start),
          stop_idx_end_(stop_idx_end),
          bitfield_idx_(bitfield_idx) {}

    transport_idx_t const transport_idx_{};
    unsigned const stop_idx_start_{};
    unsigned const stop_idx_end_{};
    bitfield_idx_t const bitfield_idx_{};
  };

  void enqueue(transport_idx_t const transport_idx,
               unsigned const stop_idx,
               bitfield_idx_t const& bf,
               unsigned const n);

  std::vector<std::size_t> q_cur_;
  std::vector<std::size_t> q_start_;
  std::vector<transport_segment> q_;
  // Q_n data structure - END
};

}  // namespace nigiri::routing::tripbased