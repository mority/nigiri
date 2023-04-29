#pragma once

#include <cinttypes>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
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
    constexpr r_entry(transport_idx_t const transport_idx,
                      unsigned const stop_idx,
                      bitfield_idx_t const bitfield_idx)
        : transport_idx_(transport_idx),
          stop_idx_(stop_idx),
          bitfield_idx_(bitfield_idx) {}

    transport_idx_t transport_idx_{};
    unsigned stop_idx_{};
    bitfield_idx_t bitfield_idx_{};
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
               bitfield const& bf,
               unsigned const n);

  // q_cur_[n] = cursor of Q_n
  std::vector<std::size_t> q_cur_ = {0U};
  // q_start_[n] = start of Q_n
  std::vector<std::size_t> q_start_ = {0U};
  // q_end_[n] = end of Q_n (exclusive)
  std::vector<std::size_t> q_end_ = {0U};
  // all Q_n back to back
  std::vector<transport_segment> q_;
  // Q_n data structure - END

  struct l_entry {
    l_entry(route_idx_t const route_idx,
            unsigned const stop_idx,
            duration_t const time)
        : route_idx_(route_idx), stop_idx_(stop_idx), time_(time) {}

    route_idx_t const route_idx_{};
    unsigned const stop_idx_;
    duration_t time_{};
  };

  // routes that reach the target stop
  std::vector<l_entry> l_;

  // result set of pareto-optimal journeys
  pareto_set<journey> j_;

  // for a bitset with only one bit set to one, returns the index of this bit
  constexpr unsigned day_idx(bitfield const&);

  void reset();

  void earliest_arrival_query(query);
};

}  // namespace nigiri::routing::tripbased