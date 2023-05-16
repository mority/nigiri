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
  explicit tb_query(tb_preprocessing& tbp) : tbp_(tbp), tt_(tbp.tt_) {
    // queue size is boundend by number of elementary connections in the time
    // table
    std::cout << "Number of elementary connections is " << tbp_.num_el_con_
              << ", reserving " << tbp_.num_el_con_ * sizeof(transport_segment)
              << " bytes of memory for q_" << std::endl;
    q_.reserve(tbp_.num_el_con_);
  }

  tb_preprocessing& tbp_;
  timetable& tt_;

  // R(t) data structure - BEGIN
  struct r_entry {
    std::uint16_t get_stop_idx() const {
      return static_cast<uint16_t>(stop_idx_);
    }

    day_idx_t get_day_idx_start() const { return day_idx_t{day_idx_start_}; }

    day_idx_t get_day_idx_end() const { return day_idx_t{day_idx_end_}; }

    std::uint32_t stop_idx_ : STOP_IDX_BITS;
    std::uint32_t day_idx_start_ : DAY_IDX_BITS;
    std::uint32_t day_idx_end_ : DAY_IDX_BITS;
  };

  void r_update(transport_idx_t const,
                std::uint16_t const stop_idx,
                day_idx_t const);

  std::uint16_t r_query(transport_idx_t const, day_idx_t const);

  mutable_fws_multimap<transport_idx_t, r_entry> r_{};
  // R(t) data structure - END

  // Q_n data structure - BEGIN
  // for reconstruction of the journey
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

#define TRANSFERRED_FROM_NULL std::numeric_limits<std::uint32_t>::max()

  void enqueue(transport_idx_t const transport_idx,
               std::uint16_t const stop_idx,
               day_idx_t const day_idx,
               std::uint32_t const n,
               std::uint32_t const transferred_from);

  // q_cur_[n] = cursor of Q_n
  std::vector<std::uint32_t> q_cur_ = {0U};
  // q_start_[n] = start of Q_n
  std::vector<std::uint32_t> q_start_ = {0U};
  // q_end_[n] = end of Q_n (exclusive)
  std::vector<std::uint32_t> q_end_ = {0U};
  // all Q_n back to back
  std::vector<transport_segment> q_;
  // Q_n data structure - END

  struct l_entry {
    l_entry(route_idx_t const route_idx,
            std::uint16_t const stop_idx,
            duration_t const time)
        : route_idx_(route_idx), stop_idx_(stop_idx), time_(time) {}

    // the route index of the route that reaches the target location
    route_idx_t const route_idx_{};
    // the stop index at which the route reaches the target location
    std::uint16_t const stop_idx_{};
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

  void reset();

  void earliest_arrival_query(query);

  // reconstructs the journey that ends with the given transport segment
  void reconstruct_journey(transport_segment const& last_tp_seg, journey& j);
};

}  // namespace nigiri::routing::tripbased