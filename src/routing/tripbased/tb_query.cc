#include <ranges>

#include "nigiri/routing/tripbased/tb_query.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query::r_update(transport_idx_t const transport_idx,
                        std::uint16_t const stop_idx,
                        day_idx_t day_idx) {

  auto day_idx_end = std::numeric_limits<day_idx_t>::max();

  for (auto re : r_[transport_idx]) {
    if (stop_idx < re.stop_idx_) {
      if (day_idx <= re.day_idx_start_) {
        // TODO this should be a deletion
        re.day_idx_start_ = re.day_idx_end_;
      } else if (day_idx < re.day_idx_end_) {
        re.day_idx_end_ = day_idx;
      }
    } else if (stop_idx == re.stop_idx_) {
      if (day_idx < re.day_idx_start_) {
        // TODO this should be a deletion
        re.day_idx_start_ = re.day_idx_end_;
      } else {
        // new entry already covered by current entry
        return;
      }
    } else if (day_idx < re.day_idx_start_) {
      day_idx_end = re.day_idx_start_;
    } else {
      // new entry already covered by current entry
      return;
    }
  }

  r_[transport_idx].emplace_back(stop_idx, day_idx, day_idx_end);
}

std::uint16_t tb_query::r_query(transport_idx_t const transport_idx,
                                day_idx_t const day_idx) {

  // look up stop index for the provided day index
  for (auto re : r_[transport_idx]) {
    if (re.day_idx_start_ <= day_idx && day_idx < re.day_idx_end_) {
      return re.stop_idx_;
    }
  }

  // no entry for this transport_idx/day_idx combination
  // return stop index of final stop of the transport
  return static_cast<uint16_t>(
      tt_.route_location_seq_[tt_.transport_route_[transport_idx]].size() - 1);
}

void tb_query::enqueue(transport_idx_t const transport_idx,
                       std::uint16_t const stop_idx,
                       day_idx_t const day_idx,
                       std::uint32_t const n,
                       std::uint32_t const transferred_from) {
  auto const r_query_res = r_query(transport_idx, day_idx);
  if (stop_idx < r_query_res) {

    // new n?
    if (n == q_cur_.size()) {
      q_cur_.emplace_back(q_.size());
      q_start_.emplace_back(q_.size());
      q_end_.emplace_back(q_.size());
    }

    // add transport segment
    q_.emplace_back(transport_idx, stop_idx, r_query_res, day_idx,
                    transferred_from);
    ++q_end_[n];

    // update all transports of this route
    auto const route_idx = tt_.transport_route_[transport_idx];
    for (auto const transport_idx_it : tt_.route_transport_ranges_[route_idx]) {
      // construct d_new
      auto day_idx_new = day_idx;

      // set the bit of the day of the instance to false if the current
      // transport is earlier than the newly enqueued
      auto const mam_u = tt_.event_mam(transport_idx_it, 0U, event_type::kDep);
      auto const mam_t = tt_.event_mam(transport_idx, 0U, event_type::kDep);
      if (mam_u < mam_t) {
        ++day_idx_new;
      }

      r_update(transport_idx_it, stop_idx, day_idx_new);
    }
  }
}

constexpr int tb_query::bitfield_to_day_idx(bitfield const& bf) {
  assert(bf.count() == 1);
  unsigned short i{0U};
  for (; i != bf.size(); ++i) {
    if (bf.test(i)) {
      break;
    }
  }
  return i;
}

void tb_query::reset() {
  r_.clear();
  q_cur_.clear();
  q_cur_.emplace_back(0U);
  q_start_.clear();
  q_start_.emplace_back(0U);
  q_end_.clear();
  q_end_.emplace_back(0U);
  l_.clear();
  j_.clear();
}

void tb_query::earliest_arrival_query(nigiri::routing::query query) {
  reset();

  query_ = query;

  // holds source location
  auto const offset_source = query.start_[0];
  // holds target location
  auto const offset_target = query.destinations_[0][0];

  // departure time
  auto const start_unixtime = cista::get_if<unixtime_t>(query.start_time_);
  assert(start_unixtime);
  auto const day_idx_mam_pair = tt_.day_idx_mam(*start_unixtime);
  // day index of start day
  auto const day_idx = day_idx_mam_pair.first;
  // minutes after midnight on the start day
  auto const start_mam = day_idx_mam_pair.second;

  // bit set identifying the start day
  bitfield start_day{};
  start_day.set(day_idx.v_);

  // fill l_
  // iterate incoming footpaths of target location
  for (auto const fp : tt_.locations_.footpaths_in_[offset_target.target_]) {
    auto const delta_tau =
        offset_target.target_ == fp.target_ ? duration_t{0U} : fp.duration_;
    // iterate routes serving source of footpath
    for (auto const route_idx : tt_.location_routes_[fp.target_]) {
      // iterate stop sequence of route
      for (auto stop_idx{0U};
           stop_idx < tt_.route_location_seq_[route_idx].size(); ++stop_idx) {
        auto const stop =
            timetable::stop{tt_.route_location_seq_[route_idx][stop_idx]};
        if (stop.location_idx() == fp.target_) {
          l_.emplace_back(route_idx, stop_idx, delta_tau);
        }
      }
    }
  }

  // fill Q_0
  // iterate outgoing footpaths of source location
  for (auto const fp : tt_.locations_.footpaths_out_[offset_source.target_]) {
    // arrival time after walking the footpath
    auto const t_a = start_mam + fp.duration_;
    // shift amount due to walking the footpath
    auto const sa_fp = num_midnights(t_a);
    // time of day after walking the footpath
    auto const a = time_of_day(t_a);
    // iterate routes at target stop of footpath
    for (auto const route_idx : tt_.location_routes_[fp.target_]) {
      // iterate stop sequence of route, skip last stop
      for (std::uint16_t stop_idx = 0U;
           stop_idx < tt_.route_location_seq_[route_idx].size() - 1;
           ++stop_idx) {
        auto const stop =
            timetable::stop{tt_.route_location_seq_[route_idx][stop_idx]};
        if (stop.location_idx() == fp.target_) {
          // shift amount due to waiting for connection
          day_idx_t sa_w{0U};
          // departure times of this route at this stop
          auto const event_times =
              tt_.event_times_at_stop(route_idx, stop_idx, event_type::kDep);
          // iterator to departure time of connecting transport at this stop
          auto dep_it =
              std::lower_bound(event_times.begin(), event_times.end(), a,
                               [&](auto&& x, auto&& y) {
                                 return time_of_day(x) < time_of_day(y);
                               });
          // no departure found on the day of a
          if (dep_it == event_times.end()) {
            // start looking at the following day
            ++sa_w;
            dep_it = event_times.begin();
          }
          // iterate departures until maximum waiting time is reached
          while (sa_w <= tbp_.sa_w_max_) {
            // shift amount due to travel time of transport
            auto const sa_t = num_midnights(*dep_it);
            // total shift amount
            auto const day_idx_seg = day_idx + sa_fp + sa_w - sa_t;
            // offset of connecting transport in route_transport_ranges
            auto const offset_transport =
                std::distance(event_times.begin(), dep_it);
            // transport_idx_t of the connecting transport
            auto const transport_idx =
                tt_.route_transport_ranges_[route_idx][static_cast<std::size_t>(
                    offset_transport)];
            // bitfield of the connecting transport
            auto const& bf_transport =
                tt_.bitfields_[tt_.transport_traffic_days_[transport_idx]];

            if (bf_transport.test(day_idx_seg.v_)) {
              // enqueue segment if a matching bit is found
              enqueue(transport_idx, stop_idx, day_idx_seg, 0U,
                      std::numeric_limits<std::uint32_t>::max());
              break;
            }
            // passing midnight?
            if (dep_it + 1 == event_times.end()) {
              ++sa_w;
              // start with the earliest transport on the next day
              dep_it = event_times.begin();
            } else {
              ++dep_it;
            }
          }
        }
      }
    }
  }

  // minimal travel time observed so far
  auto t_min = duration_t::max();

  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (auto n{0U}; n != q_start_.size(); ++n) {
    // iterate trip segments in Q_n
    for (; q_cur_[n] < q_end_[n]; ++q_cur_[n]) {
      // the current transport segment
      auto tp_seg = q_[q_cur_[n]];
      // departure time at the start of the transport segment
      auto const tp_seg_dep = tt_.event_mam(
          tp_seg.transport_idx_, tp_seg.stop_idx_start_, event_type::kDep);
      // departure time at start of current transport segment in minutes after
      // midnight on the day of the query
      auto const dep_query =
          duration_t{
              (tp_seg.day_idx_.v_ + num_midnights(tp_seg_dep).v_ - day_idx.v_) *
              1440U} +
          time_of_day(tp_seg_dep);

      // check if target location is reached from current transport segment
      for (auto const& le : l_) {
        if (le.route_idx_ == tt_.transport_route_[tp_seg.transport_idx_] &&
            tp_seg.stop_idx_start_ < le.stop_idx_) {
          // the time it takes to travel on this transport segment
          auto const travel_time_seg =
              tt_.event_mam(tp_seg.transport_idx_, le.stop_idx_,
                            event_type::kArr) -
              tp_seg_dep;
          // the time at which the target location is reached by using the
          // current transport segment
          auto const t_cur = dep_query + travel_time_seg + le.time_;
          if (t_cur < t_min) {
            t_min = t_cur;
            // change end stop index of the current transport segment to match
            // the target location
            tp_seg.stop_idx_end_ = le.stop_idx_;
            // reconstruct journey
            journey j{};
            reconstruct_journey(tp_seg, j);
            // add journey to pareto set (removes dominated entries)
            j_.add(std::move(j));
          }
        }
      }

      // the time it takes to travel to the next stop of the transport segment
      auto const travel_time_next =
          tt_.event_mam(tp_seg.transport_idx_, tp_seg.stop_idx_start_ + 1,
                        event_type::kArr) -
          tp_seg_dep;

      // transfer out of current transport segment?
      if (dep_query + travel_time_next < t_min) {
        // iterate stops of the current transport segment
        for (auto stop_idx{tp_seg.stop_idx_start_ + 1U};
             stop_idx <= tp_seg.stop_idx_end_; ++stop_idx) {
          auto const ts_pair =
              tbp_.ts_.get_transfers(tp_seg.transport_idx_, stop_idx);
          // check if there are transfers from this stop
          if (ts_pair.has_value()) {
            auto const transfers_start = ts_pair->first;
            auto const transfers_end = ts_pair->second;
            // iterate transfers from this stop
            for (auto transfer_idx = transfers_start;
                 transfer_idx != transfers_end; ++transfer_idx) {
              // the current transfer
              auto const& transfer_cur = tbp_.ts_[transfer_idx];
              // bitset specifying the days on which the transfer is possible
              // from the current transport segment
              auto const& bf_transfer =
                  tt_.bitfields_[transfer_cur.bitfield_idx_];
              // enqueue if transfer is possible
              if (bf_transfer.test(tp_seg.day_idx_.v_)) {
                // arrival time at start location of transfer
                auto const time_arr = tt_.event_mam(tp_seg.transport_idx_,
                                                    stop_idx, event_type::kArr);
                // departure time at end location of transfer
                auto const time_dep =
                    tt_.event_mam(transfer_cur.transport_idx_to_,
                                  transfer_cur.stop_idx_to_, event_type::kDep);

                auto const day_index_transfer =
                    tp_seg.day_idx_ + num_midnights(time_arr) -
                    num_midnights(time_dep) + transfer_cur.passes_midnight_;
                enqueue(transfer_cur.transport_idx_to_,
                        static_cast<std::uint16_t>(transfer_cur.stop_idx_to_),
                        day_index_transfer, n + 1U, q_cur_[n]);
              }
            }
          }
        }
      }
    }
  }
}

void tb_query::reconstruct_journey(
    tb_query::transport_segment const& last_tp_seg, journey& j) {

  // the transport segment currently processed
  auto const* tp_seg = &last_tp_seg;

  // follow transferred_from_ back to the start of the journey
  while (true) {

    // the route index of the current segment
    auto const& route_idx = tt_.transport_route_[tp_seg->transport_idx_];
    // the location index of the start of the segment
    auto const location_idx_start =
        timetable::stop{
            tt_.route_location_seq_[route_idx][tp_seg->stop_idx_start_]}
            .location_idx();
    // unix time: departure time at the start of the segment
    auto const unix_start =
        tt_.to_unixtime(tp_seg->day_idx_, tt_.event_mam(tp_seg->transport_idx_,
                                                        tp_seg->stop_idx_start_,
                                                        event_type::kDep));
    // the stop index of the end of the segment
    auto stop_idx_end = tp_seg->stop_idx_end_;
    // arrival time at the end of the segment
    auto time_arr_end =
        tt_.event_mam(tp_seg->transport_idx_, stop_idx_end, event_type::kArr);
    // unix time: arrival time at the end of the segment
    auto unix_end = tt_.to_unixtime(tp_seg->day_idx_, time_arr_end);
    // the location index of the end of the segment
    auto location_idx_end =
        timetable::stop{tt_.route_location_seq_[route_idx][stop_idx_end]}
            .location_idx();

    // reconstruct transfer to following segment, i.e., the back of j.legs_
    if (!j.legs_.empty()) {
      // increment number of transfers
      ++j.transfers_;
      // rescan for transfer to next leg
      for (std::uint16_t stop_idx = tp_seg->stop_idx_start_ + 1U;
           stop_idx <= tp_seg->stop_idx_end_; ++stop_idx) {
        // get transfer indices for this stop
        auto const ts_pair =
            tbp_.ts_.get_transfers(tp_seg->transport_idx_, stop_idx);
        // iterate transfers for this stop
        for (auto tr_idx = ts_pair->first; tr_idx < ts_pair->second; ++tr_idx) {
          auto& tr_cur = tbp_.ts_[tr_idx];
          if (tr_cur.transport_idx_to_ ==
                  get<journey::transport_enter_exit>(j.legs_.back().uses_)
                      .t_.t_idx_ &&
              tr_cur.stop_idx_to_ ==
                  get<journey::transport_enter_exit>(j.legs_.back().uses_)
                      .stop_range_.from_ &&
              tt_.bitfields_[tr_cur.bitfield_idx_].test(tp_seg->day_idx_.v_)) {
            // set end stop index of current segment according to found transfer
            if (stop_idx_end != stop_idx) {
              stop_idx_end = stop_idx;
              // recalculate because end stop idx changed
              time_arr_end = tt_.event_mam(tp_seg->transport_idx_, stop_idx_end,
                                           event_type::kArr);
              unix_end = tt_.to_unixtime(tp_seg->day_idx_, time_arr_end);
              location_idx_end =
                  timetable::stop{
                      tt_.route_location_seq_[route_idx][stop_idx_end]}
                      .location_idx();
            }
            // create footpath journey leg for the transfer
            for (auto const fp :
                 tt_.locations_.footpaths_out_[location_idx_end]) {
              // check if footpath reaches the start of the next journey leg
              if (fp.target_ == j.legs_.back().from_) {
                // unix time: arrival at the end of the footpath
                auto const unix_fp_end = tt_.to_unixtime(
                    tp_seg->day_idx_, time_arr_end + fp.duration_);

                journey::leg l_fp{direction::kForward,  location_idx_end,
                                  j.legs_.back().from_, unix_end,
                                  unix_fp_end,          fp};
                j.add(std::move(l_fp));
                goto transfer_handled;
              }
            }
          }
        }
      }
    }

  transfer_handled:

    // add journey leg for this transport segment
    journey::transport_enter_exit tee{
        transport{tp_seg->transport_idx_, tp_seg->day_idx_},
        tp_seg->stop_idx_start_, stop_idx_end};
    journey::leg l_tp{direction::kForward, location_idx_start, location_idx_end,
                      unix_start,          unix_end,           tee};
    j.add(std::move(l_tp));

    // first segment reached?
    if (tp_seg->transferred_from_ == TRANSFERRED_FROM_NULL) {
      break;
    }

    // prep next iteration
    tp_seg = &q_[tp_seg->transferred_from_];
  }

  // reverse order of journey legs
  std::reverse(j.legs_.begin(), j.legs_.end());

  // set journey attributes
  j.start_time_ = j.legs_.front().dep_time_;
  j.dest_time_ = j.legs_.back().arr_time_;
  j.dest_ = j.legs_.back().to_;
  // transfers are counted when creating the legs of the journey above
}
