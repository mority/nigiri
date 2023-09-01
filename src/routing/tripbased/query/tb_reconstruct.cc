#include <ranges>

#include "nigiri/routing/for_each_meta.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/query/query_engine.h"
#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/routing/tripbased/transfer_class.h"
#include "nigiri/rt/frun.h"
#include "nigiri/special_stations.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void query_engine::reconstruct(query const& q, journey& j) const {
#ifndef NDEBUG
  TBDL << "Beginning reconstruction of journey: ";
  j.print(std::cout, tt_, nullptr, true);
#endif

  // find journey end
  auto je = reconstruct_journey_end(q, j);
  if (!je.has_value()) {
    std::cerr
        << "Journey reconstruction failed: Could not find a journey end\n";
    return;
  }

  if (q.dest_match_mode_ == location_match_mode::kIntermodal) {
    // set destination to END if routing to intermodal
    j.dest_ = get_special_station(special_station::kEnd);
  } else {
    // set reconstructed destination if routing to station
    j.dest_ = je->last_location_;
  }

  // add final footpath
  add_final_footpath(q, j, je.value());

  // init current segment with last segment
  std::optional<transport_segment> seg = state_.q_n_.segments_[je->seg_idx_];

  // set end stop of current segment according to l_entry
  seg->stop_idx_end_ = je->le_.stop_idx_;

  // journey leg for last segment
  add_segment_leg(j, seg.value());

  auto n = j.transfers_;

  // add segments and transfers until first segment is reached
  while (seg->transferred_from_ != TRANSFERRED_FROM_NULL) {
    // reduce number of transfers
    --n;

    // reconstruct transfer to following segment, returns the segment from which
    // we transferred (iteration step)
    seg = reconstruct_transfer(j, seg.value(), n);
    if (!seg.has_value()) {
      std::cerr
          << "Journey reconstruction failed: Could not reconstruct transfer\n";
      std::reverse(j.legs_.begin(), j.legs_.end());
      return;
    }

    // add journey leg for current segment
    add_segment_leg(j, seg.value());
  }

  // add initial footpath
  add_initial_footpath(q, j);

  // reverse order of journey legs
  std::reverse(j.legs_.begin(), j.legs_.end());
#ifndef NDEBUG
  TBDL << "Completed reconstruction of journey: ";
  j.print(std::cout, tt_, nullptr, true);
#endif
}

std::optional<query_engine::journey_end> query_engine::reconstruct_journey_end(
    query const& q, journey const& j) const {

  // iterate transport segments in queue with matching number of transfers
  for (auto q_cur = state_.q_n_.start_[j.transfers_];
       q_cur != state_.q_n_.end_[j.transfers_]; ++q_cur) {
    // the transport segment currently processed
    auto const& seg = state_.q_n_[q_cur];

    // the route index of the current segment
    auto const route_idx = tt_.transport_route_[seg.get_transport_idx()];

    // find matching entry in l_
    for (auto const& le : state_.route_dest_) {
      // check if route and stop indices match
      if (le.route_idx_ == route_idx && seg.stop_idx_start_ < le.stop_idx_ &&
          le.stop_idx_ <= seg.stop_idx_end_) {
        // transport day of the segment
        auto const transport_day = seg.get_transport_day(base_);
        // transport time at destination
        auto const transport_time =
            tt_.event_mam(seg.get_transport_idx(), le.stop_idx_,
                          event_type::kArr)
                .count() +
            le.time_;
        // day at destination
        auto const day_at_dest =
            transport_day + static_cast<std::uint16_t>(transport_time / 1440);
        // unix_time of segment at destination
        auto const seg_unix_dest = tt_.to_unixtime(
            day_at_dest, minutes_after_midnight_t{transport_time % 1440});
        // check if time at destination matches
        if (seg_unix_dest == j.dest_time_) {
          // the location specified by the l_entry
          auto const le_location_idx =
              stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                  .location_idx();

          // to station
          if (q.dest_match_mode_ != location_match_mode::kIntermodal) {
            if (le.time_ == 0 && is_dest_[le_location_idx.v_]) {
              // either the location of the l_entry is itself a destination
              return journey_end{q_cur, le, le_location_idx, le_location_idx};
            } else {
              // or there exists a footpath with matching duration to a
              // destination
              for (auto const& fp :
                   tt_.locations_.footpaths_out_[le_location_idx]) {
                if (fp.duration().count() == le.time_ &&
                    is_dest_[fp.target().v_]) {
                  return journey_end{q_cur, le, le_location_idx, fp.target()};
                }
              }
            }
            // to intermodal
          } else {
            if (le.time_ == dist_to_dest_[le_location_idx.v_]) {
              // either the location of the l_entry itself has matching
              // dist_to_dest
              return journey_end{q_cur, le, le_location_idx, le_location_idx};
            } else {
              // or there exists a footpath such that footpath duration and
              // dist_to_dest of footpath target match the time of the l_entry
              for (auto const& fp :
                   tt_.locations_.footpaths_out_[le_location_idx]) {
                if (fp.duration().count() + dist_to_dest_[fp.target().v_] ==
                    le.time_) {
                  return journey_end{q_cur, le, le_location_idx, fp.target()};
                }
              }
            }
          }
        }
      }
    }
  }
  // if no journey end could be found
  return std::nullopt;
}

void query_engine::add_final_footpath(query const& q,
                                      journey& j,
                                      journey_end const& je) const {

  if (q.dest_match_mode_ == location_match_mode::kIntermodal) {
#ifndef NDEBUG
    TBDL << "Adding final footpath, destination match mode: intermodal\n";
#endif
    // add MUMO to END in case of intermodal routing
    for (auto const& offset : q.destination_) {
      // find offset for last location
      if (offset.target() == je.last_location_) {
        j.add(journey::leg{direction::kForward, je.last_location_, j.dest_,
                           j.dest_time_ - offset.duration(), j.dest_time_,
                           offset});
#ifndef NDEBUG
        TBDL << "Adding final multi-modal leg:\n";
        j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
        break;
      }
    }
    // add footpath if location of l_entry and journey end destination differ
    if (!matches(tt_, q.dest_match_mode_, je.le_location_, je.last_location_)) {
      for (auto const fp : tt_.locations_.footpaths_out_[je.le_location_]) {
        if (fp.target() == je.last_location_) {
          unixtime_t const fp_time_end = j.legs_.back().dep_time_;
          unixtime_t const fp_time_start = fp_time_end - fp.duration();
          j.add(journey::leg{direction::kForward, je.le_location_,
                             je.last_location_, fp_time_start, fp_time_end,
                             fp});
#ifndef NDEBUG
          TBDL << "Adding final footpath to a destination:\n";
          j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
          break;
        }
      }
    }
  } else {
    // to station routing
#ifndef NDEBUG
    TBDL << "Adding final footpath, destination match mode: not intermodal\n";
#endif
    if (je.last_location_ == je.le_location_) {
      //    if (matches(tt_, q.dest_match_mode_, je.le_location_,
      //    je.last_location_)) {
      // add footpath with duration = 0 if destination is reached directly
      footpath const fp{je.last_location_, duration_t{0}};
      j.add(journey::leg{direction::kForward, je.last_location_,
                         je.last_location_, j.dest_time_, j.dest_time_, fp});
#ifndef NDEBUG
      TBDL << "Directly reaches a destination, adding reflexive footpath with "
              "zero duration:\n";
      j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
    } else {
      // add footpath between location of l_entry and destination
      for (auto const fp : tt_.locations_.footpaths_out_[je.le_location_]) {
        if (fp.target() == je.last_location_) {
          j.add(journey::leg{direction::kForward, je.le_location_,
                             je.last_location_, j.dest_time_ - fp.duration(),
                             j.dest_time_, fp});
#ifndef NDEBUG
          TBDL << "Adding final footpath to a destination:\n";
          j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
          break;
        }
      }
    }
  }
}

void query_engine::add_segment_leg(journey& j,
                                   transport_segment const& seg) const {
  auto const from =
      stop{
          tt_.route_location_seq_[tt_.transport_route_[seg.get_transport_idx()]]
                                 [seg.get_stop_idx_start()]}
          .location_idx();
  auto const to =
      stop{
          tt_.route_location_seq_[tt_.transport_route_[seg.get_transport_idx()]]
                                 [seg.get_stop_idx_end()]}
          .location_idx();
  auto const dep_time =
      tt_.to_unixtime(seg.get_transport_day(base_),
                      tt_.event_mam(seg.get_transport_idx(),
                                    seg.stop_idx_start_, event_type::kDep)
                          .as_duration());
  auto const arr_time =
      tt_.to_unixtime(seg.get_transport_day(base_),
                      tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_end_,
                                    event_type::kArr)
                          .as_duration());
  j.add(journey::leg{direction::kForward, from, to, dep_time, arr_time,
                     journey::run_enter_exit{
                         rt::run{transport{seg.get_transport_idx(),
                                           seg.get_transport_day(base_)},
                                 interval<stop_idx_t>{0, 0}},
                         seg.get_stop_idx_start(), seg.get_stop_idx_end()}});
#ifndef NDEBUG
  TBDL << "Adding a leg for a transport segment:\n";
  j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
}

std::optional<transport_segment> query_engine::reconstruct_transfer(
    journey& j, transport_segment const& seg_next, std::uint8_t n) const {
  assert(std::holds_alternative<journey::run_enter_exit>(j.legs_.back().uses_));

  // data on target of transfer
  auto const target_transport_idx =
      std::get<journey::run_enter_exit>(j.legs_.back().uses_).r_.t_.t_idx_;
  auto const target_location_idx = j.legs_.back().from_;
  auto const target_day_idx = seg_next.get_transport_day(base_);

  // reconstruct footpath
  std::optional<footpath> fp_transfer = std::nullopt;
  std::optional<location_idx_t> exit_location_idx = std::nullopt;
  std::optional<stop_idx_t> exit_stop_idx = std::nullopt;

  // attempt to reconstruct the transfer, use the transferred_from segment as
  // initial candidate
  for (auto q_idx = seg_next.transferred_from_; q_idx != state_.q_n_.end_[n];
       ++q_idx) {
    // copy current segment to avoid clobbering
    auto seg = state_.q_n_.segments_[q_idx];

    fp_transfer = std::nullopt;

    // iterate stops of segment
    for (stop_idx_t stop_idx_exit = seg.get_stop_idx_start() + 1U;
         stop_idx_exit <= seg.get_stop_idx_end(); ++stop_idx_exit) {
      exit_stop_idx = stop_idx_exit;
      exit_location_idx =
          stop{tt_.route_location_seq_
                   [tt_.transport_route_[seg.get_transport_idx()]]
                   [stop_idx_exit]}
              .location_idx();

      // iterate transfers at each stop
      for (auto const& transfer :
           state_.ts_.at(seg.get_transport_idx().v_, stop_idx_exit)) {

        // check transport idx matches
        auto const transfer_transport_idx = transfer.get_transport_idx_to();
        if (transfer_transport_idx != target_transport_idx) {
          continue;
        }

        // check location idx matches
        auto const transfer_stop_idx = transfer.get_stop_idx_to();
        auto const transfer_location_idx =
            stop{tt_.route_location_seq_
                     [tt_.transport_route_[transfer_transport_idx]]
                     [transfer_stop_idx]}
                .location_idx();
        if (transfer_location_idx != target_location_idx) {
          continue;
        }

        // check transport day matches
        auto const transfer_day_idx = day_idx_t{
            static_cast<std::int16_t>(seg.get_transport_day(base_).v_) +
            tt_.event_mam(seg.get_transport_idx(), stop_idx_exit,
                          event_type::kArr)
                .days() -
            tt_.event_mam(transfer_transport_idx, transfer_stop_idx,
                          event_type::kDep)
                .days() +
            static_cast<std::int16_t>(transfer.get_passes_midnight().v_)};
        if (transfer_day_idx != target_day_idx) {
          continue;
        }

        // check if transfer is active on the day of the segment
        if (!tt_.bitfields_[transfer.get_bitfield_idx()].test(
                seg.get_transport_day(base_).v_)) {
          continue;
        }

        // reconstruct footpath between stops
        if (exit_location_idx == target_location_idx) {
          // exit and target are equal -> use minimum transfer time
          fp_transfer = footpath{
              exit_location_idx.value(),
              tt_.locations_.transfer_time_[exit_location_idx.value()]};
        } else {
          for (auto const& fp :
               tt_.locations_.footpaths_out_[exit_location_idx.value()]) {
            if (fp.target() == target_location_idx) {
              fp_transfer = fp;
              break;
            }
          }
        }

// check if additional criteria match
#ifndef TB_CACHE_PRESSURE_REDUCTION
        // if cache pressure reduction is ON, the walking time and transfer
        // class is stored with each segment. Hence, there can be no reading
        // of a clobbered value from reached
#ifdef TB_MIN_WALK
        auto const reached_walk =
            state_.r_.walk(seg.transport_segment_idx_, n, seg.stop_idx_start_);
        auto const reached_walk_next = state_.r_.walk(
            seg_next.transport_segment_idx_, n + 1U, seg_next.stop_idx_start_);

        auto const transfer_walk =
            exit_location_idx.value() == target_location_idx
                ? 0U
                : static_cast<std::uint16_t>(fp_transfer->duration().count());

        if (reached_walk + transfer_walk != reached_walk_next) {
#ifndef NDEBUG
          TBDL << "Walking time criterion mismatch, continuing scan..\n";
#endif
          // walking time does not match, continue scanning for another
          // transfer that caused the improved walking time
          fp_transfer = std::nullopt;
          continue;
        }
#elifdef TB_TRANSFER_CLASS
        auto const reached_transfer_class = state_.r_.transfer_class(
            seg.transport_segment_idx_, n, seg.stop_idx_start_);
        auto const reached_transfer_class_max = reached_transfer_class.first;
        auto const reached_transfer_class_sum = reached_transfer_class.second;

        auto const reached_transfer_class_next = state_.r_.transfer_class(
            seg_next.transport_segment_idx_, n + 1U, seg_next.stop_idx_start_);
        auto const reached_transfer_class_max_next =
            reached_transfer_class_next.first;
        auto const reached_transfer_class_sum_next =
            reached_transfer_class_next.second;

        auto const candidate_transfer_class = transfer_class(transfer_wait(
            tt_, seg.get_transport_idx(), exit_stop_idx.value(),
            seg_next.get_transport_idx(), seg_next.stop_idx_start_,
            seg.get_transport_day(base_).v_,
            seg_next.get_transport_day(base_).v_));

        if (reached_transfer_class_max > reached_transfer_class_max_next ||
            candidate_transfer_class > reached_transfer_class_max_next ||
            reached_transfer_class_sum + candidate_transfer_class !=
                reached_transfer_class_sum_next) {
#ifndef NDEBUG
          TBDL << "Transfer class criteria mismatch, continuing scan...\n";
#endif
          // transfer class criteria do not match, continue scanning for
          // another transfer that caused the improved transfer class criteria
          fp_transfer = std::nullopt;
          continue;
        }
#endif
#endif
        goto found_footpath;
      }
    }
    if (!fp_transfer.has_value()) {
      // no transfer from current segment candidate possible
      continue;
    }

  found_footpath:

    // add footpath leg for transfer
    auto const fp_time_start =
        tt_.to_unixtime(seg.get_transport_day(base_),
                        tt_.event_mam(seg.get_transport_idx(),
                                      exit_stop_idx.value(), event_type::kArr)
                            .as_duration());
    auto const fp_time_end = fp_time_start + fp_transfer->duration();
    j.add(journey::leg{direction::kForward, exit_location_idx.value(),
                       target_location_idx, fp_time_start, fp_time_end,
                       fp_transfer.value()});
#ifndef NDEBUG
    TBDL << "Adding a leg for a transfer:\n";
    j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif

    // set the stop idx at which the segment is exited
    seg.stop_idx_end_ = exit_stop_idx.value();
    return seg;
  }
  // all segments candidates in current q_n were examined, but there was no
  // match
  return std::nullopt;
}

void query_engine::add_initial_footpath(query const& q, journey& j) const {
  // check if first transport departs at start location
  if (!is_start_location(q, j.legs_.back().from_)) {
    // first transport does not start at a start location
    // -> add footpath leg
    std::optional<footpath> fp_initial = std::nullopt;
    auto duration_min = duration_t::max();
    for (auto const& fp : tt_.locations_.footpaths_in_[j.legs_.back().from_]) {
      // choose the shortest initial footpath if there is more than one
      if (is_start_location(q, fp.target()) && fp.duration() < duration_min) {
        duration_min = fp.duration();
        fp_initial = fp;
      }
    }
    if (!fp_initial.has_value()) {
      std::cerr << "Journey reconstruction failed: Could reconstruct initial "
                   "footpath\n";
      return;
    }
    j.add(journey::leg{direction::kForward, fp_initial->target(),
                       j.legs_.back().from_,
                       j.legs_.back().dep_time_ - fp_initial->duration(),
                       j.legs_.back().dep_time_, fp_initial.value()});
#ifndef NDEBUG
    TBDL << "Adding an initial footpath:\n";
    j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
  }

  if (q.start_match_mode_ == location_match_mode::kIntermodal) {
#ifndef NDEBUG
    TBDL << "Start match mode: intermodal\n";
#endif
    for (auto const& offset : q.start_) {
      // add MUMO from START to first journey leg
      if (offset.target() == j.legs_.back().from_) {
        unixtime_t const mumo_start_unix{j.legs_.back().dep_time_ -
                                         offset.duration()};
        j.add(journey::leg{direction::kForward,
                           get_special_station(special_station::kStart),
                           offset.target(), mumo_start_unix,
                           j.legs_.back().dep_time_, offset});
#ifndef NDEBUG
        TBDL << "Adding an initial multi-modal leg:\n";
        j.legs_.back().print(std::cout, tt_, nullptr, 0U, true);
#endif
        return;
      }
    }
    std::cerr << "Journey reconstruction failed: Could not reconstruct initial "
                 "MUMO edge\n";
  }
}

bool query_engine::is_start_location(query const& q,
                                     location_idx_t const l) const {
  bool res = false;
  for (auto const& offset : q.start_) {
    res = matches(tt_, q.start_match_mode_, offset.target(), l);
    if (res) {
      break;
    }
  }
  return res;
}