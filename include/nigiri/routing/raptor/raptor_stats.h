#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace nigiri::routing {

struct raptor_stats {
  std::map<std::string, std::uint64_t> to_map() const {
    return {
        {"n_routing_time", n_routing_time_},
        {"n_footpaths_visited", n_footpaths_visited_},
        {"n_routes_visited", n_routes_visited_},
        {"n_earliest_trip_calls", n_earliest_trip_calls_},
        {"n_earliest_arrival_updated_by_route",
         n_earliest_arrival_updated_by_route_},
        {"n_earliest_arrival_updated_by_footpath",
         n_earliest_arrival_updated_by_footpath_},
        {"fp_update_prevented_by_lower_bound",
         fp_update_prevented_by_lower_bound_},
        {"route_update_prevented_by_lower_bound",
         route_update_prevented_by_lower_bound_},
        {"n_slot_labels", n_slot_labels_},
        {"n_slot_rejects", n_slot_rejects_},
        {"n_slot_journeys", n_slot_journeys_},
        {"n_alt_results", n_alt_results_},
        {"n_alt_journeys", n_alt_journeys_},
        {"n_alt_attempts", n_alt_attempts_},
        {"n_alt_failed", n_alt_failed_},
        {"n_alt_duplicates", n_alt_duplicates_},
        {"n_alt_capped", n_alt_capped_},
        {"n_alt_micros", n_alt_micros_},
        {"n_alt_min_skip_1", n_alt_min_skip_1_},
        {"n_alt_min_skip_2", n_alt_min_skip_2_},
        {"n_alt_min_skip_3", n_alt_min_skip_3_},
        {"n_alt_min_skip_4_7", n_alt_min_skip_4_7_},
        {"n_alt_min_skip_ge_8", n_alt_min_skip_ge_8_},
        {"n_sb_executes", n_sb_executes_},
        {"n_sb_candidates", n_sb_candidates_},
        {"n_sb_second", n_sb_second_},
        {"n_sb_evicted", n_sb_evicted_},
        {"n_sb_worse", n_sb_worse_},
        {"n_sb_equal", n_sb_equal_},
        {"n_sb_bound_relaxed", n_sb_bound_relaxed_},
        {"n_sb_dest_reached", n_sb_dest_reached_},
        {"n_sb_dest_second", n_sb_dest_second_},
        {"n_sb_dest_delta_le_5", n_sb_dest_delta_le_5_},
        {"n_sb_dest_delta_le_15", n_sb_dest_delta_le_15_},
        {"n_sb_dest_delta_le_30", n_sb_dest_delta_le_30_},
        {"n_sb_dest_delta_le_60", n_sb_dest_delta_le_60_},
        {"n_sb_dest_delta_gt_60", n_sb_dest_delta_gt_60_},
        {"n_sb_dest_same_round", n_sb_dest_same_round_},
        {"n_sb_dest_more_transfers", n_sb_dest_more_transfers_},
        {"n_sb_dest_fewer_transfers", n_sb_dest_fewer_transfers_},
        {"n_sb_dest_same_round_le_15", n_sb_dest_same_round_le_15_},
        {"n_sb_dest_more_transfers_le_15", n_sb_dest_more_transfers_le_15_},
        {"n_sb_dest_fewer_transfers_le_15", n_sb_dest_fewer_transfers_le_15_},
    };
  }

  raptor_stats operator+(raptor_stats const& o) const {
    auto copy = *this;
    copy.n_routing_time_ += o.n_routing_time_;
    copy.n_footpaths_visited_ += o.n_footpaths_visited_;
    copy.n_routes_visited_ += o.n_routes_visited_;
    copy.n_earliest_trip_calls_ += o.n_earliest_trip_calls_;
    copy.n_earliest_arrival_updated_by_route_ +=
        o.n_earliest_arrival_updated_by_route_;
    copy.n_earliest_arrival_updated_by_footpath_ +=
        o.n_earliest_arrival_updated_by_footpath_;
    copy.fp_update_prevented_by_lower_bound_ +=
        o.fp_update_prevented_by_lower_bound_;
    copy.route_update_prevented_by_lower_bound_ +=
        o.route_update_prevented_by_lower_bound_;
    copy.n_slot_labels_ += o.n_slot_labels_;
    copy.n_slot_rejects_ += o.n_slot_rejects_;
    copy.n_slot_journeys_ += o.n_slot_journeys_;
    copy.n_alt_results_ += o.n_alt_results_;
    copy.n_alt_journeys_ += o.n_alt_journeys_;
    copy.n_alt_attempts_ += o.n_alt_attempts_;
    copy.n_alt_failed_ += o.n_alt_failed_;
    copy.n_alt_duplicates_ += o.n_alt_duplicates_;
    copy.n_alt_capped_ += o.n_alt_capped_;
    copy.n_alt_micros_ += o.n_alt_micros_;
    copy.n_alt_min_skip_1_ += o.n_alt_min_skip_1_;
    copy.n_alt_min_skip_2_ += o.n_alt_min_skip_2_;
    copy.n_alt_min_skip_3_ += o.n_alt_min_skip_3_;
    copy.n_alt_min_skip_4_7_ += o.n_alt_min_skip_4_7_;
    copy.n_alt_min_skip_ge_8_ += o.n_alt_min_skip_ge_8_;
    copy.n_sb_executes_ += o.n_sb_executes_;
    copy.n_sb_candidates_ += o.n_sb_candidates_;
    copy.n_sb_second_ += o.n_sb_second_;
    copy.n_sb_evicted_ += o.n_sb_evicted_;
    copy.n_sb_worse_ += o.n_sb_worse_;
    copy.n_sb_equal_ += o.n_sb_equal_;
    copy.n_sb_bound_relaxed_ += o.n_sb_bound_relaxed_;
    copy.n_sb_dest_reached_ += o.n_sb_dest_reached_;
    copy.n_sb_dest_second_ += o.n_sb_dest_second_;
    copy.n_sb_dest_delta_le_5_ += o.n_sb_dest_delta_le_5_;
    copy.n_sb_dest_delta_le_15_ += o.n_sb_dest_delta_le_15_;
    copy.n_sb_dest_delta_le_30_ += o.n_sb_dest_delta_le_30_;
    copy.n_sb_dest_delta_le_60_ += o.n_sb_dest_delta_le_60_;
    copy.n_sb_dest_delta_gt_60_ += o.n_sb_dest_delta_gt_60_;
    copy.n_sb_dest_same_round_ += o.n_sb_dest_same_round_;
    copy.n_sb_dest_more_transfers_ += o.n_sb_dest_more_transfers_;
    copy.n_sb_dest_fewer_transfers_ += o.n_sb_dest_fewer_transfers_;
    copy.n_sb_dest_same_round_le_15_ += o.n_sb_dest_same_round_le_15_;
    copy.n_sb_dest_more_transfers_le_15_ += o.n_sb_dest_more_transfers_le_15_;
    copy.n_sb_dest_fewer_transfers_le_15_ += o.n_sb_dest_fewer_transfers_le_15_;
    return copy;
  }

  std::uint64_t n_routing_time_{0ULL};
  std::uint64_t n_footpaths_visited_{0ULL};
  std::uint64_t n_routes_visited_{0ULL};
  std::uint64_t n_earliest_trip_calls_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_route_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_footpath_{0ULL};
  std::uint64_t fp_update_prevented_by_lower_bound_{0ULL};
  std::uint64_t route_update_prevented_by_lower_bound_{0ULL};

  // extra slot: labels written to the extra slot, labels rejected because
  // the extra slot holds a better one, journeys from the extra slot
  std::uint64_t n_slot_labels_{0ULL};
  std::uint64_t n_slot_rejects_{0ULL};
  std::uint64_t n_slot_journeys_{0ULL};

  // alternatives by reconstruction, see reconstruct_alternatives
  std::uint64_t n_alt_results_{0ULL};
  std::uint64_t n_alt_journeys_{0ULL};
  std::uint64_t n_alt_attempts_{0ULL};
  std::uint64_t n_alt_failed_{0ULL};
  std::uint64_t n_alt_duplicates_{0ULL};
  std::uint64_t n_alt_capped_{0ULL};
  std::uint64_t n_alt_micros_{0ULL};
  // results by the first combination that led to an alternative
  std::uint64_t n_alt_min_skip_1_{0ULL};
  std::uint64_t n_alt_min_skip_2_{0ULL};
  std::uint64_t n_alt_min_skip_3_{0ULL};
  std::uint64_t n_alt_min_skip_4_7_{0ULL};
  std::uint64_t n_alt_min_skip_ge_8_{0ULL};

  // second best (sb) statistics, see raptor::observe
  // candidate = label time offered to a stop, before any pruning
  std::uint64_t n_sb_executes_{0ULL};
  std::uint64_t n_sb_candidates_{0ULL};
  // rejected, exactly one better label at the stop -> slot 2 for n = 2
  std::uint64_t n_sb_second_{0ULL};
  // best label of this round overwritten by a better one -> slot 2 for n = 2
  std::uint64_t n_sb_evicted_{0ULL};
  // rejected, two or more better labels at the stop -> rejected for n = 2
  std::uint64_t n_sb_worse_{0ULL};
  std::uint64_t n_sb_equal_{0ULL};
  // pruned by the best time at the destination, but not by the second best
  std::uint64_t n_sb_bound_relaxed_{0ULL};
  // per execute: destination reached / second arrival time at destination /
  // difference between best and second best arrival time at destination
  std::uint64_t n_sb_dest_reached_{0ULL};
  std::uint64_t n_sb_dest_second_{0ULL};
  std::uint64_t n_sb_dest_delta_le_5_{0ULL};
  std::uint64_t n_sb_dest_delta_le_15_{0ULL};
  std::uint64_t n_sb_dest_delta_le_30_{0ULL};
  std::uint64_t n_sb_dest_delta_le_60_{0ULL};
  std::uint64_t n_sb_dest_delta_gt_60_{0ULL};
  // transfers of the second best arrival compared to the best arrival
  // same: alternative in the round of the fastest journey
  // more: later with more transfers, one dominator
  // fewer: part of the pareto set anyway, no alternative
  std::uint64_t n_sb_dest_same_round_{0ULL};
  std::uint64_t n_sb_dest_more_transfers_{0ULL};
  std::uint64_t n_sb_dest_fewer_transfers_{0ULL};
  std::uint64_t n_sb_dest_same_round_le_15_{0ULL};
  std::uint64_t n_sb_dest_more_transfers_le_15_{0ULL};
  std::uint64_t n_sb_dest_fewer_transfers_le_15_{0ULL};
};

}  // namespace nigiri::routing
