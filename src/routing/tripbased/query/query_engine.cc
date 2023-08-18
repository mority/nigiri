#include <ranges>

#include "nigiri/routing/for_each_meta.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/settings.h"

#ifdef TB_TRANSFER_CLASS
#include "nigiri/routing/tripbased/transfer_class.h"
#endif

#include "nigiri/routing/tripbased/query/query_engine.h"
#include "nigiri/rt/frun.h"
#include "nigiri/special_stations.h"

using namespace nigiri;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;

query_engine::query_engine(timetable const& tt,
                           rt_timetable const* rtt,
                           query_state& state,
                           std::vector<bool>& is_dest,
                           std::vector<std::uint16_t>& dist_to_dest,
                           std::vector<std::uint16_t>& lb,
                           day_idx_t const base)
    : tt_{tt},
      rtt_{rtt},
      state_{state},
      is_dest_{is_dest},
      dist_to_dest_{dist_to_dest},
      lb_{lb},
      base_{base} {

  // reset state for new query
  state_.reset(base);

  // init l_, i.e., routes that reach the destination at certain stop idx
  if (dist_to_dest.empty()) {
    // create l_entries for station-to-station query
    for (location_idx_t dest{0U}; dest != location_idx_t{is_dest_.size()};
         ++dest) {
      if (is_dest_[dest.v_]) {
        // fill l_
        auto create_l_entry = [this](footpath const& fp) {
          // iterate routes serving source of footpath
          for (auto const route_idx : tt_.location_routes_[fp.target()]) {
            // iterate stop sequence of route
            for (std::uint16_t stop_idx{1U};
                 stop_idx < tt_.route_location_seq_[route_idx].size();
                 ++stop_idx) {
              auto const location_idx =
                  stop{tt_.route_location_seq_[route_idx][stop_idx]}
                      .location_idx();
              if (location_idx == fp.target()) {
                state_.route_dest_.emplace_back(route_idx, stop_idx,
                                                fp.duration().count());
              }
            }
          }
        };
        // virtual reflexive incoming footpath
        create_l_entry(footpath{dest, duration_t{0U}});
        // iterate incoming footpaths of target location
        for (auto const fp : tt_.locations_.footpaths_in_[dest]) {
          create_l_entry(fp);
        }
      }
    }
  } else {
    // create l_entries for coord-to-coord query
    for (location_idx_t dest{0U}; dest != location_idx_t{dist_to_dest.size()};
         ++dest) {
      if (dist_to_dest[dest.v_] != std::numeric_limits<std::uint16_t>::max()) {
        // fill l_
        auto create_l_entry = [this, &dest](footpath const& fp) {
          // iterate routes serving source of footpath
          for (auto const route_idx : tt_.location_routes_[fp.target()]) {
            // iterate stop sequence of route
            for (std::uint16_t stop_idx{1U};
                 stop_idx < tt_.route_location_seq_[route_idx].size();
                 ++stop_idx) {
              auto const location_idx =
                  stop{tt_.route_location_seq_[route_idx][stop_idx]}
                      .location_idx();
              if (location_idx == fp.target()) {
                state_.route_dest_.emplace_back(
                    route_idx, stop_idx,
                    fp.duration().count() + dist_to_dest_[dest.v_]);
              }
            }
          }
        };
        // virtual reflexive incoming footpath
        create_l_entry(footpath{dest, duration_t{0U}});
        // iterate incoming footpaths of target location
        for (auto const fp : tt_.locations_.footpaths_in_[dest]) {
          create_l_entry(fp);
        }
      }
    }
  }
}

void query_engine::execute(unixtime_t const start_time,
                           std::uint8_t const max_transfers,
                           unixtime_t const worst_time_at_dest,
                           pareto_set<journey>& results) {
#ifndef NDEBUG
  TBDL << "Executing with start_time: " << unix_dhhmm(tt_, start_time)
       << ", max_transfers: " << std::to_string(max_transfers)
       << ", worst_time_at_dest: " << unix_dhhmm(tt_, worst_time_at_dest)
       << ", Initializing Q_0...\n";
#endif
  // init Q_0
  for (auto const& qs : state_.query_starts_) {
    handle_start(qs);
  }

  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (std::uint8_t n = 0U;
       n != state_.q_n_.start_.size() && n <= max_transfers; ++n) {
#ifndef NDEBUG
    TBDL << "Processing segments of Q_" << std::to_string(n) << ":\n";
#endif

#ifdef TB_CACHE_PRESSURE_REDUCTION
    // (1)  destination reached?
    for (auto q_cur = state_.q_n_.start_[n]; q_cur != state_.q_n_.end_[n];
         ++q_cur) {
#if !defined(TB_MIN_WALK) && !defined(TB_TRANSFER_CLASS)
      seg_dest(start_time, results, worst_time_at_dest, n, state_.q_n_[q_cur]);
#else
      seg_dest(start_time, results, n, state_.q_n_[q_cur]);
#endif
    }
    // (2) pruning?
    for (auto q_cur = state_.q_n_.start_[n]; q_cur != state_.q_n_.end_[n];
         ++q_cur) {
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
      seg_prune(start_time, worst_time_at_dest, results, n, state_.q_n_[q_cur]);
#else
      seg_prune(worst_time_at_dest, n, state_.q_n_[q_cur]);
#endif
    }
    // (3) process transfers & enqueue segments
    for (auto q_cur = state_.q_n_.start_[n]; q_cur != state_.q_n_.end_[n];
         ++q_cur) {
      seg_transfers(n, q_cur);
    }

#else
    // iterate trip segments in Q_n
    for (auto q_cur = state_.q_n_.start_[n]; q_cur != state_.q_n_.end_[n];
         ++q_cur) {
#ifndef NDEBUG
      TBDL << "Examining segment: ";
      state_.q_n_.print(std::cout, q_cur);
#endif
      handle_segment(start_time, worst_time_at_dest, results, n, q_cur);
    }
#endif
  }
}

#ifdef TB_CACHE_PRESSURE_REDUCTION
void query_engine::seg_dest(unixtime_t const start_time,
                            pareto_set<journey>& results,
#if !defined(TB_MIN_WALK) && !defined(TB_TRANSFER_CLASS)
                            unixtime_t worst_time_at_dest,
#endif
                            std::uint8_t const n,
                            transport_segment& seg) {

  // departure time at the start of the transport segment
  auto const tau_dep_t_b = tt_.event_mam(seg.get_transport_idx(),
                                         seg.stop_idx_start_, event_type::kDep)
                               .count();
  auto const tau_dep_t_b_d =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_,
                    event_type::kDep)
          .days();
  auto const tau_dep_t_b_tod =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_,
                    event_type::kDep)
          .mam();

  // the day index of the segment
  std::int32_t const d_seg = seg.get_transport_day(base_).v_;
  // departure time at start of current transport segment in minutes after
  // midnight on the day of the query
  auto const tau_d =
      (d_seg + tau_dep_t_b_d - base_.v_) * 1440 + tau_dep_t_b_tod;

  // check if target location is reached from current transport segment
  for (auto const& le : state_.route_dest_) {
    if (le.route_idx_ == tt_.transport_route_[seg.get_transport_idx()] &&
        seg.stop_idx_start_ < le.stop_idx_ &&
        le.stop_idx_ <= seg.stop_idx_end_) {
      // the time it takes to travel on this transport segment
      auto const travel_time_seg =
          tt_.event_mam(seg.get_transport_idx(), le.stop_idx_, event_type::kArr)
              .count() -
          tau_dep_t_b;
      // the time at which the target location is reached by using the
      // current transport segment
      auto const t_cur = tt_.to_unixtime(
          base_, minutes_after_midnight_t{tau_d + travel_time_seg + le.time_});

#ifndef NDEBUG
      TBDL << "segment reaches a destination at "
           << dhhmm(tau_d + travel_time_seg + le.time_) << "\n";
#endif
      // add journey if it is non-dominated
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
      journey j{};
      j.start_time_ = start_time;
      j.dest_time_ = t_cur;
      j.dest_ = stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                    .location_idx();
      j.transfers_ = n;
#ifdef TB_MIN_WALK
      j.time_walk_ = seg.time_walk_ + le.time_;
#elifdef TB_TRANSFER_CLASS
      j.transfer_class_max_ = seg.transfer_class_max_;
      j.transfer_class_sum_ = seg.transfer_class_sum_;
#endif
      results.add(std::move(j));
#else
      if (t_cur < state_.t_min_[n] && t_cur < worst_time_at_dest) {
        state_.t_min_[n] = t_cur;
        // add journey without reconstructing yet
        journey j{};
        j.start_time_ = start_time;
        j.dest_time_ = t_cur;
        j.dest_ = stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                      .location_idx();
        j.transfers_ = n;
        // add journey to pareto set (removes dominated entries)
#ifndef NDEBUG
        TBDL << "updating pareto set with new journey: ";
        j.print(std::cout, tt_);
        auto [non_dominated, begin, end] = results.add(std::move(j));
        if (non_dominated) {
          TBDL << "new journey ending with this segment is non-dominated\n";
        } else {
          TBDL << "new journey ending with this segment is dominated\n";
        }
#else
        results.add(std::move(j));
#endif
      }
#endif
    }
  }

  // the time it takes to travel to the next stop of the transport segment
  auto const travel_time_next =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_ + 1,
                    event_type::kArr)
          .count() -
      tau_dep_t_b;

  // the unix time at the next stop of the transport segment
  seg.time_prune_ = tt_.to_unixtime(
      base_, minutes_after_midnight_t{tau_d + travel_time_next});
}

void query_engine::seg_prune(
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
    unixtime_t const start_time,
#endif
    unixtime_t const worst_time_at_dest,
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
    pareto_set<journey>& results,
#endif
    std::uint8_t const n,
    transport_segment& seg) {
  seg.no_prune_ = seg.time_prune_ < worst_time_at_dest;
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
  if (seg.no_prune_) {
    journey tentative_j{};
    tentative_j.start_time_ = start_time;
    tentative_j.dest_time_ = seg.time_prune_;
    tentative_j.transfers_ = n + 1;
#ifdef TB_MIN_WALK
    tentative_j.time_walk_ = seg.time_walk_;
#elifdef TB_TRANSFER_CLASS
    tentative_j.transfer_class_max_ = seg.transfer_class_max_;
    tentative_j.transfer_class_sum_ = seg.transfer_class_sum_;
#endif
    for (auto const& existing_j : results) {
      if (existing_j.dominates(tentative_j)) {
        seg.no_prune_ = false;
        break;
      }
    }
  }
#else
  seg.no_prune_ = no_prune && seg.time_prune_ < state_.t_min_[n];
#endif
}

void query_engine::seg_transfers(std::uint8_t const n,
                                 queue_idx_t const q_cur) {

  auto const& seg = state_.q_n_[q_cur];

  // transfer out of current transport segment?
  if (seg.no_prune_) {

#ifndef NDEBUG
    TBDL << "Time at next stop of segment is viable\n";
#endif

    // the day index of the segment
    std::int32_t const d_seg = seg.get_transport_day(base_).v_;

    // iterate stops of the current transport segment
    for (stop_idx_t i = seg.get_stop_idx_start() + 1U;
         i <= seg.get_stop_idx_end(); ++i) {
#ifndef NDEBUG
      TBDL << "Arrival at stop " << i << ": "
           << location_name(
                  tt_,
                  stop{tt_.route_location_seq_
                           [tt_.transport_route_[seg.get_transport_idx()]][i]}
                      .location_idx())
           << " at "
           << unix_dhhmm(
                  tt_, tt_.to_unixtime(seg.get_transport_day(base_),
                                       tt_.event_mam(seg.get_transport_idx(), i,
                                                     event_type::kArr)
                                           .as_duration()))
           << ", processing transfers...\n";
#endif

      // get transfers for this transport/stop
      auto const& transfers =
          state_.ts_.data_.at(seg.get_transport_idx().v_, i);
      // iterate transfers from this stop
      for (auto const& transfer : transfers) {
        // bitset specifying the days on which the transfer is possible
        // from the current transport segment
        auto const& theta = tt_.bitfields_[transfer.get_bitfield_idx()];
        // enqueue if transfer is possible
        if (theta.test(static_cast<std::size_t>(d_seg))) {
          // arrival time at start location of transfer
          auto const tau_arr_t_i =
              tt_.event_mam(seg.get_transport_idx(), i, event_type::kArr)
                  .count();
          // departure time at end location of transfer
          auto const tau_dep_u_j =
              tt_.event_mam(transfer.get_transport_idx_to(),
                            transfer.stop_idx_to_, event_type::kDep)
                  .count();

          auto const d_tr = d_seg + tau_arr_t_i / 1440 - tau_dep_u_j / 1440 +
                            transfer.passes_midnight_;
#ifndef NDEBUG
          TBDL << "Found a transfer to transport "
               << transfer.get_transport_idx_to() << ": "
               << tt_.transport_name(transfer.get_transport_idx_to())
               << " at its stop " << transfer.stop_idx_to_ << ": "
               << location_name(tt_,
                                stop{tt_.route_location_seq_
                                         [tt_.transport_route_
                                              [transfer.get_transport_idx_to()]]
                                         [transfer.get_stop_idx_to()]}
                                    .location_idx())
               << ", departing at "
               << unix_dhhmm(tt_,
                             tt_.to_unixtime(
                                 day_idx_t{d_tr},
                                 tt_.event_mam(transfer.get_transport_idx_to(),
                                               transfer.get_stop_idx_to(),
                                               event_type::kDep)
                                     .as_duration()))
               << "\n";
#endif

#ifdef TB_MIN_WALK
          auto const p_t_i =
              stop{tt_.route_location_seq_
                       [tt_.transport_route_[seg.get_transport_idx()]][i]}
                  .location_idx();
          auto const p_u_j =
              stop{tt_.route_location_seq_
                       [tt_.transport_route_[transfer.get_transport_idx_to()]]
                       [transfer.get_stop_idx_to()]}
                  .location_idx();

          std::uint16_t walk_time = seg.time_walk_;
          if (p_t_i != p_u_j) {
            for (auto const& fp : tt_.locations_.footpaths_out_[p_t_i]) {
              if (fp.target() == p_u_j) {
                walk_time += fp.duration_;
              }
            }
          }

          state_.q_n_.enqueue_walk(
              static_cast<std::uint16_t>(d_tr), transfer.get_transport_idx_to(),
              transfer.get_stop_idx_to(), n + 1U, walk_time, q_cur);
#elifdef TB_TRANSFER_CLASS
          auto const kappa = transfer_class(transfer_wait(
              tt_, seg.get_transport_idx(), i, transfer.get_transport_idx_to(),
              transfer.get_stop_idx_to(), static_cast<const uint16_t>(d_seg),
              static_cast<const uint16_t>(d_tr)));
          state_.q_n_.enqueue_class(static_cast<std::uint16_t>(d_tr),
                                    transfer.get_transport_idx_to(),
                                    transfer.get_stop_idx_to(), n + 1U,
                                    std::max(kappa, seg.transfer_class_max_),
                                    seg.transfer_class_sum_ + kappa, q_cur);
#else
          state_.q_n_.enqueue(static_cast<std::uint16_t>(d_tr),
                              transfer.get_transport_idx_to(),
                              transfer.get_stop_idx_to(), n + 1U, q_cur);
#endif
        }
      }
    }
  }
}

#else

void query_engine::handle_segment(unixtime_t const start_time,
                                  unixtime_t const worst_time_at_dest,
                                  pareto_set<journey>& results,
                                  std::uint8_t const n,
                                  queue_idx_t const q_cur) {
  // the current transport segment
  auto seg = state_.q_n_[q_cur];

#ifndef NDEBUG
  TBDL << "Examining segment: ";
  state_.q_n_.print(std::cout, q_cur);
#endif

  // departure time at the start of the transport segment
  auto const tau_dep_t_b = tt_.event_mam(seg.get_transport_idx(),
                                         seg.stop_idx_start_, event_type::kDep)
                               .count();
  auto const tau_dep_t_b_d =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_,
                    event_type::kDep)
          .days();
  auto const tau_dep_t_b_tod =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_,
                    event_type::kDep)
          .mam();

  // the day index of the segment
  std::int32_t const d_seg = seg.get_transport_day(base_).v_;
  // departure time at start of current transport segment in minutes after
  // midnight on the day of the query
  auto const tau_d =
      (d_seg + tau_dep_t_b_d - base_.v_) * 1440 + tau_dep_t_b_tod;

#ifdef TB_MIN_WALK
  auto const reached_walk =
      state_.r_.walk(seg.transport_segment_idx_, n, seg.get_stop_idx_start());
#elifdef TB_TRANSFER_CLASS
  auto const reached_transfer_class = state_.r_.transfer_class(
      seg.transport_segment_idx_, n, seg.stop_idx_start_);
  auto const reached_transfer_class_max = reached_transfer_class.first;
  auto const reached_transfer_class_sum = reached_transfer_class.second;
#endif

  // check if target location is reached from current transport segment
  for (auto const& le : state_.route_dest_) {
    if (le.route_idx_ == tt_.transport_route_[seg.get_transport_idx()] &&
        seg.stop_idx_start_ < le.stop_idx_ &&
        le.stop_idx_ <= seg.stop_idx_end_) {
      // the time it takes to travel on this transport segment
      auto const travel_time_seg =
          tt_.event_mam(seg.get_transport_idx(), le.stop_idx_, event_type::kArr)
              .count() -
          tau_dep_t_b;
      // the time at which the target location is reached by using the
      // current transport segment
      auto const t_cur = tt_.to_unixtime(
          base_, minutes_after_midnight_t{tau_d + travel_time_seg + le.time_});

#ifndef NDEBUG
      TBDL << "segment reaches a destination at "
           << dhhmm(tau_d + travel_time_seg + le.time_) << "\n";
#endif

#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
      // add journey if it is non-dominated
      journey j{};
      j.start_time_ = start_time;
      j.dest_time_ = t_cur;
      j.dest_ = stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                    .location_idx();
      j.transfers_ = n;
#ifdef TB_MIN_WALK
      j.time_walk_ = reached_walk + le.time_;
#elifdef TB_TRANSFER_CLASS
      j.transfer_class_max_ = reached_transfer_class_max;
      j.transfer_class_sum_ = reached_transfer_class_sum;
#endif
      results.add(std::move(j));
#else
      if (t_cur < state_.t_min_[n] && t_cur < worst_time_at_dest) {
        state_.t_min_[n] = t_cur;
        // add journey without reconstructing yet
        journey j{};
        j.start_time_ = start_time;
        j.dest_time_ = t_cur;
        j.dest_ = stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                      .location_idx();
        j.transfers_ = n;
        // add journey to pareto set (removes dominated entries)
#ifndef NDEBUG
        TBDL << "updating pareto set with new journey: ";
        j.print(std::cout, tt_);
        auto [non_dominated, begin, end] = results.add(std::move(j));
        if (non_dominated) {
          TBDL << "new journey ending with this segment is non-dominated\n";
        } else {
          TBDL << "new journey ending with this segment is dominated\n";
        }
#else
        results.add(std::move(j));
#endif
      }
#endif
    }
  }

  // the time it takes to travel to the next stop of the transport segment
  auto const travel_time_next =
      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_ + 1,
                    event_type::kArr)
          .count() -
      tau_dep_t_b;

  // the unix time at the next stop of the transport segment
  auto const unix_time_next = tt_.to_unixtime(
      base_, minutes_after_midnight_t{tau_d + travel_time_next});

  bool no_prune = unix_time_next < worst_time_at_dest;
#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
  if (no_prune) {
    journey tentative_j{};
    tentative_j.start_time_ = start_time;
    tentative_j.dest_time_ = unix_time_next;
    tentative_j.transfers_ = n + 1;
#ifdef TB_MIN_WALK
    tentative_j.time_walk_ = reached_walk;
#elifdef TB_TRANSFER_CLASS
    tentative_j.transfer_class_max_ = reached_transfer_class_max;
    tentative_j.transfer_class_sum_ = reached_transfer_class_sum;
#endif
    for (auto const& existing_j : results) {
      if (existing_j.dominates(tentative_j)) {
        no_prune = false;
        break;
      }
    }
  }
#else
  no_prune = no_prune && unix_time_next < state_.t_min_[n];
#endif
  // transfer out of current transport segment?
  if (no_prune) {
#ifndef NDEBUG
    TBDL << "Time at next stop of segment is viable\n";
#endif

    // iterate stops of the current transport segment
    for (stop_idx_t i = seg.get_stop_idx_start() + 1U;
         i <= seg.get_stop_idx_end(); ++i) {
#ifndef NDEBUG
      TBDL << "Arrival at stop " << i << ": "
           << location_name(
                  tt_,
                  stop{tt_.route_location_seq_
                           [tt_.transport_route_[seg.get_transport_idx()]][i]}
                      .location_idx())
           << " at "
           << unix_dhhmm(
                  tt_, tt_.to_unixtime(seg.get_transport_day(base_),
                                       tt_.event_mam(seg.get_transport_idx(), i,
                                                     event_type::kArr)
                                           .as_duration()))
           << ", processing transfers...\n";
#endif

      // get transfers for this transport/stop
      auto const& transfers =
          state_.ts_.data_.at(seg.get_transport_idx().v_, i);
      // iterate transfers from this stop
      for (auto const& transfer : transfers) {
        // bitset specifying the days on which the transfer is possible
        // from the current transport segment
        auto const& theta = tt_.bitfields_[transfer.get_bitfield_idx()];
        // enqueue if transfer is possible
        if (theta.test(static_cast<std::size_t>(d_seg))) {
          // arrival time at start location of transfer
          auto const tau_arr_t_i =
              tt_.event_mam(seg.get_transport_idx(), i, event_type::kArr)
                  .count();
          // departure time at end location of transfer
          auto const tau_dep_u_j =
              tt_.event_mam(transfer.get_transport_idx_to(),
                            transfer.stop_idx_to_, event_type::kDep)
                  .count();

          auto const d_tr = d_seg + tau_arr_t_i / 1440 - tau_dep_u_j / 1440 +
                            transfer.passes_midnight_;
#ifndef NDEBUG
          TBDL << "Found a transfer to transport "
               << transfer.get_transport_idx_to() << ": "
               << tt_.transport_name(transfer.get_transport_idx_to())
               << " at its stop " << transfer.stop_idx_to_ << ": "
               << location_name(tt_,
                                stop{tt_.route_location_seq_
                                         [tt_.transport_route_
                                              [transfer.get_transport_idx_to()]]
                                         [transfer.get_stop_idx_to()]}
                                    .location_idx())
               << ", departing at "
               << unix_dhhmm(tt_,
                             tt_.to_unixtime(
                                 day_idx_t{d_tr},
                                 tt_.event_mam(transfer.get_transport_idx_to(),
                                               transfer.get_stop_idx_to(),
                                               event_type::kDep)
                                     .as_duration()))
               << "\n";
#endif

#ifdef TB_MIN_WALK
          auto const p_t_i =
              stop{tt_.route_location_seq_
                       [tt_.transport_route_[seg.get_transport_idx()]][i]}
                  .location_idx();
          auto const p_u_j =
              stop{tt_.route_location_seq_
                       [tt_.transport_route_[transfer.get_transport_idx_to()]]
                       [transfer.get_stop_idx_to()]}
                  .location_idx();

          std::uint16_t walk_time = state_.r_.walk(seg.transport_segment_idx_,
                                                   n, seg.stop_idx_start_);
          if (p_t_i != p_u_j) {
            for (auto const& fp : tt_.locations_.footpaths_out_[p_t_i]) {
              if (fp.target() == p_u_j) {
                walk_time += fp.duration_;
              }
            }
          }

          state_.q_n_.enqueue_walk(
              static_cast<std::uint16_t>(d_tr), transfer.get_transport_idx_to(),
              transfer.get_stop_idx_to(), n + 1U, walk_time, q_cur);
#elifdef TB_TRANSFER_CLASS
          auto const kappa = transfer_class(transfer_wait(
              tt_, seg.get_transport_idx(), i, transfer.get_transport_idx_to(),
              transfer.get_stop_idx_to(), static_cast<const uint16_t>(d_seg),
              static_cast<const uint16_t>(d_tr)));
          state_.q_n_.enqueue_class(static_cast<std::uint16_t>(d_tr),
                                    transfer.get_transport_idx_to(),
                                    transfer.get_stop_idx_to(), n + 1U,
                                    std::max(kappa, reached_transfer_class_max),
                                    reached_transfer_class_sum + kappa, q_cur);
#else
          state_.q_n_.enqueue(static_cast<std::uint16_t>(d_tr),
                              transfer.get_transport_idx_to(),
                              transfer.get_stop_idx_to(), n + 1U, q_cur);
#endif
        }
      }
    }
  }
}

#endif

void query_engine::handle_start(query_start const& start) {

  // start day and time
  auto const day_idx_mam = tt_.day_idx_mam(start.time_);
  // start day
  std::int32_t const d = day_idx_mam.first.v_;
  // start time
  std::int32_t const tau = day_idx_mam.second.count();

#ifndef NDEBUG
  TBDL << "handle_start | start_location: "
       << location_name(tt_, start.location_)
       << " | start_time: " << dhhmm(d * 1440 + tau) << "\n";
#endif

  // virtual reflexive footpath
#ifndef NDEBUG
  TBDL << "Examining routes at start location: "
       << location_name(tt_, start.location_) << "\n";
#endif
  handle_start_footpath(d, tau, footpath{start.location_, duration_t{0U}});
  // iterate outgoing footpaths of source location
  for (auto const fp : tt_.locations_.footpaths_out_[start.location_]) {
#ifndef NDEBUG
    TBDL << "Examining routes at location: " << location_name(tt_, fp.target())
         << " reached after walking " << fp.duration() << " minutes"
         << "\n";
#endif
    handle_start_footpath(d, tau, fp);
  }
}

void query_engine::handle_start_footpath(std::int32_t const d,
                                         std::int32_t const tau,
                                         footpath const fp) {
  // arrival time after walking the footpath
  auto const alpha = tau + fp.duration().count();
  auto const alpha_d = alpha / 1440;
  auto const alpha_tod = alpha % 1440;

  // iterate routes at target stop of footpath
  for (auto const route_idx : tt_.location_routes_[fp.target()]) {
#ifndef NDEBUG
    TBDL << "Route " << route_idx << "\n";
#endif
    // iterate stop sequence of route, skip last stop
    for (std::uint16_t i = 0U;
         i < tt_.route_location_seq_[route_idx].size() - 1; ++i) {
      auto const q = stop{tt_.route_location_seq_[route_idx][i]}.location_idx();
      if (q == fp.target()) {
#ifndef NDEBUG
        TBDL << "serves " << location_name(tt_, fp.target())
             << " at stop idx = " << i << "\n";
#endif
        // departure times of this route at this q
        auto const event_times =
            tt_.event_times_at_stop(route_idx, i, event_type::kDep);
        // iterator to departure time of connecting transport at this
        // q
        auto tau_dep_t_i =
            std::lower_bound(event_times.begin(), event_times.end(), alpha_tod,
                             [&](auto&& x, auto&& y) { return x.mam() < y; });
        // shift amount due to walking the footpath
        auto sigma = alpha_d;
        // no departure found on the day of alpha
        if (tau_dep_t_i == event_times.end()) {
          // start looking at the following day
          ++sigma;
          tau_dep_t_i = event_times.begin();
        }
        // iterate departures until maximum waiting time is reached
        while (sigma <= 1) {
          // shift amount due to travel time of transport
          std::int32_t const sigma_t = tau_dep_t_i->days();
          // day index of the transport segment
          auto const d_seg = d + sigma - sigma_t;
          // offset of connecting transport in route_transport_ranges
          auto const k = static_cast<std::size_t>(
              std::distance(event_times.begin(), tau_dep_t_i));
          // transport_idx_t of the connecting transport
          auto const t = tt_.route_transport_ranges_[route_idx][k];
          // bitfield of the connecting transport
          auto const& beta_t = tt_.bitfields_[tt_.transport_traffic_days_[t]];

          if (beta_t.test(static_cast<std::size_t>(d_seg))) {
            // enqueue segment if matching bit is found
#ifndef NDEBUG
            TBDL << "Attempting to enqueue a segment of transport " << t << ": "
                 << tt_.transport_name(t) << ", departing at "
                 << unix_dhhmm(tt_, tt_.to_unixtime(day_idx_t{d_seg},
                                                    tau_dep_t_i->as_duration()))
                 << "\n";
#endif
#ifdef TB_MIN_WALK
            state_.q_n_.enqueue_walk(static_cast<std::uint16_t>(d_seg), t, i,
                                     0U, fp.duration_, TRANSFERRED_FROM_NULL);
#elifdef TB_TRANSFER_CLASS
            state_.q_n_.enqueue_class(static_cast<std::uint16_t>(d_seg), t, i,
                                      0U, 0, 0, TRANSFERRED_FROM_NULL);
#else
            state_.q_n_.enqueue(static_cast<std::uint16_t>(d_seg), t, i, 0U,
                                TRANSFERRED_FROM_NULL);
#endif
            break;
          }
          // passing midnight?
          if (tau_dep_t_i + 1 == event_times.end()) {
            ++sigma;
            // start with the earliest transport on the next day
            tau_dep_t_i = event_times.begin();
          } else {
            ++tau_dep_t_i;
          }
        }
      }
    }
  }
}