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
  explicit tb_query(tb_preprocessing& tbp) : tbp_(tbp), tt_(tbp.tt_) {}

  tb_preprocessing& tbp_;
  timetable& tt_;

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
  // for reconstruction of the journey
  struct transferred_from {
    // the index of the previous transport segment in q_
    unsigned const q_idx_{};
    // the stop index at which we transferred out of the previous segment
    unsigned const stop_idx_{};
  };

  struct transport_segment {
    transport_segment(transport_idx_t const transport_idx,
                      unsigned const stop_idx_start,
                      unsigned const stop_idx_end,
                      bitfield_idx_t const bitfield_idx,
                      std::optional<transferred_from> const transferred_from)
        : transport_idx_(transport_idx),
          stop_idx_start_(stop_idx_start),
          stop_idx_end_(stop_idx_end),
          bitfield_idx_(bitfield_idx),
          transferred_from_(transferred_from) {}

    transport_idx_t const transport_idx_{};
    unsigned const stop_idx_start_{};
    unsigned stop_idx_end_{};
    bitfield_idx_t const bitfield_idx_{};

    // from which segment and how we transferred to this segment
    std::optional<transferred_from> const transferred_from_{};
  };

  void enqueue(transport_idx_t const transport_idx,
               unsigned const stop_idx,
               bitfield const& bf,
               unsigned const n,
               std::optional<transferred_from> const transferred_from);

  // q_cur_[n] = cursor of Q_n
  std::vector<unsigned> q_cur_ = {0U};
  // q_start_[n] = start of Q_n
  std::vector<unsigned> q_start_ = {0U};
  // q_end_[n] = end of Q_n (exclusive)
  std::vector<unsigned> q_end_ = {0U};
  // all Q_n back to back
  std::vector<transport_segment> q_;
  // Q_n data structure - END

  struct l_entry {
    l_entry(route_idx_t const route_idx,
            unsigned const stop_idx,
            duration_t const time)
        : route_idx_(route_idx), stop_idx_(stop_idx), time_(time) {}

    // the route index of the route that reaches the target location
    route_idx_t const route_idx_{};
    // the stop index at which the route reaches the target location
    unsigned const stop_idx_{};
    // the time it takes after exiting the route until the target location is
    // reached
    duration_t time_{};
  };

  // routes that reach the target stop
  std::vector<l_entry> l_;

  // result set of pareto-optimal journeys
  pareto_set<journey> j_;

  // least recently processed query
  query query_;

  // for a bitset with only one bit set to one, returns the index of this bit
  static constexpr int bitfield_to_day_idx(bitfield const&);

  void reset();

  void earliest_arrival_query(query);

  // reconstructs the journey that ends with the given transport segment
  void reconstruct_journey(transport_segment const& last_tp_seg, journey& j);
};

}  // namespace nigiri::routing::tripbased