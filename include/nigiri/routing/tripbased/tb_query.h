#pragma once

#include <cinttypes>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/routing/tripbased/tb_query_state.h"

#define TRANSFERRED_FROM_NULL std::numeric_limits<std::uint32_t>::max()

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

struct tb_query_stats {};

struct tb_query {
  using algo_state_t = tb_query_state;
  using algo_stats_t = tb_query_stats;

  static constexpr bool kUseLowerBounds = false;

  //  tb_query(tb_preprocessing& tbp) : tbp_(tbp), tt_(tbp.tt_) {
  //    // reserve space
  //    std::cout << "Reserving "
  //              << tt_.transport_route_.size() * 4 * sizeof(r_entry)
  //              << " bytes for data_\n";
  //    r_.reserve(tt_.transport_route_.size(), 4);
  //    l_.reserve(128);
  //    start_.reserve(16);
  //    end_.reserve(16);
  //
  //    // queue size is bounded by number of elementary connections in the
  //    // timetable
  //    std::cout << "Number of elementary connections is " << tbp_.num_el_con_
  //              << ", reserving " << tbp_.num_el_con_ *
  //              sizeof(transport_segment)
  //              << " bytes of memory for segments_" << std::endl;
  //    segments_.reserve(tbp_.num_el_con_);
  //  }

  tb_query(timetable const& tt,
           tb_query_state& state,
           std::vector<bool>& is_dest,
           std::vector<std::uint16_t>& dist_to_dest,
           std::vector<std::uint16_t>& lb,
           day_idx_t const base)
      : tt_{tt},
        state_{state},
        is_dest_{is_dest},
        dist_to_dest_{dist_to_dest},
        lb_{lb},
        base_{base} {}

  void reset_q();

  void earliest_arrival_query(query);

  // reconstructs the journey that ends with the given transport segment
  void reconstruct_journey(transport_segment const& last_tp_seg, journey& j);

  timetable const& tt_;
  tb_query_state& state_;
  std::vector<bool>& is_dest_;
  std::vector<std::uint16_t>& dist_to_dest_;
  std::vector<std::uint16_t>& lb_;
  day_idx_t const base_;
};

}  // namespace nigiri::routing::tripbased