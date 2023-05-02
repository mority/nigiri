#include <ranges>

#include "nigiri/routing/tripbased/tb_query.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query::r_update(transport_idx_t const transport_idx,
                        unsigned const stop_idx,
                        bitfield const& bf) {
  bitfield bf_new = bf;

  // find first tuple of this transport_idx
  auto r_cur = std::lower_bound(
      r_.begin(), r_.end(), transport_idx,
      [](r_entry const& re, transport_idx_t const& tpi) constexpr {
        return re.transport_idx_ < tpi;
      });

  // no entry for this transport_idx
  if (r_cur == r_.end()) {
    r_.emplace_back(transport_idx, stop_idx, tbp_.get_or_create_bfi(bf_new));
    return;
  } else if (transport_idx < r_cur->transport_idx_) {
    r_.emplace(r_cur, transport_idx, stop_idx, tbp_.get_or_create_bfi(bf_new));
    return;
  }

  // remember first erasure as possible overwrite spot
  auto overwrite_spot = r_.end();

  while (r_cur != r_.end() && r_cur->transport_idx_ == transport_idx) {
    if (bf_new.none()) {
      // early termination
      return;
    } else if (stop_idx < r_cur->stop_idx_) {
      // new stop index is better than current
      auto const bf_cur = tt_.bitfields_[r_cur->bitfield_idx_] & ~bf_new;
      if (bf_cur.none()) {
        if (overwrite_spot == r_.end()) {
          overwrite_spot = r_cur;
          ++r_cur;
        } else {
          r_.erase(r_cur);
        }
      } else {
        r_cur->bitfield_idx_ = tbp_.get_or_create_bfi(bf_cur);
        ++r_cur;
      }
    } else if (stop_idx == r_cur->stop_idx_) {
      // new stop index is equal to current
      bf_new |= tt_.bitfields_[r_cur->bitfield_idx_];
      if (overwrite_spot == r_.end()) {
        overwrite_spot = r_cur;
        ++r_cur;
      } else {
        r_.erase(r_cur);
      }
    } else {
      // new stop index is worse than current
      bf_new &= ~tt_.bitfields_[r_cur->bitfield_idx_];
      ++r_cur;
    }
  }

  if (bf_new.any()) {
    if (overwrite_spot == r_.end()) {
      r_.emplace(r_cur, transport_idx, stop_idx,
                 tbp_.get_or_create_bfi(bf_new));
    } else {
      overwrite_spot->stop_idx_ = stop_idx;
      overwrite_spot->bitfield_idx_ = tbp_.get_or_create_bfi(bf_new);
    }
  }
}

unsigned tb_query::r_query(transport_idx_t const transport_idx,
                           bitfield const& bf_query) {

  // find first entry for this transport_idx
  auto r_cur = std::lower_bound(
      r_.begin(), r_.end(), transport_idx,
      [](r_entry const& re, transport_idx_t const& tpi) constexpr {
        return re.transport_idx_ < tpi;
      });

  // find matching entry for provided bitfield
  while (r_cur != r_.end() && transport_idx == r_cur->transport_idx_) {
    if ((tt_.bitfields_[r_cur->bitfield_idx_] & bf_query).any()) {
      return r_cur->stop_idx_;
    }
    ++r_cur;
  }

  // no entry for this transport_idx
  // return stop index of final stop of the transport
  return tt_.route_location_seq_[tt_.transport_route_[transport_idx]].size() -
         1;
}

void tb_query::enqueue(const transport_idx_t transport_idx,
                       const unsigned int stop_idx,
                       bitfield const& bf,
                       const unsigned int n,
                       transport_segment const* transferred_from) {
  auto const r_query_res = r_query(transport_idx, bf);
  if (stop_idx < r_query_res) {

    // new n?
    if (n == q_cur_.size()) {
      q_cur_.emplace_back(q_.size());
      q_start_.emplace_back(q_.size());
      q_end_.emplace_back(q_.size());
    }

    // add transport segment
    q_.emplace_back(transport_idx, stop_idx, r_query_res,
                    tbp_.get_or_create_bfi(bf), transferred_from);
    ++q_end_[n];

    // construct bf_new
    auto k{0U};
    for (; k < bf.size(); ++k) {
      if (bf.test(k)) {
        break;
      }
    }
    bitfield bf_new = ~bitfield{} << k;

    // update all transports of this route
    auto const route_idx = tt_.transport_route_[transport_idx];
    for (auto const transport_idx_it : tt_.route_transport_ranges_[route_idx]) {

      // set the bit of the day of the instance to false if the current
      // transport is earlier than the newly enqueued
      auto const mam_u = tt_.event_mam(transport_idx_it, 0U, event_type::kDep);
      auto const mam_t = tt_.event_mam(transport_idx, 0U, event_type::kDep);
      if (mam_u < mam_t) {
        bf_new.set(k, false);
      } else {
        bf_new.set(k, true);
      }

      r_update(transport_idx_it, stop_idx, bf_new);
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
      for (auto stop_idx{0U};
           stop_idx < tt_.route_location_seq_[route_idx].size() - 1;
           ++stop_idx) {
        auto const stop =
            timetable::stop{tt_.route_location_seq_[route_idx][stop_idx]};
        if (stop.location_idx() == fp.target_) {
          // shift amount due to waiting for connection
          int sa_w = 0;
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
            auto const sa_total = sa_fp + sa_w - sa_t;
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
            // bitfield that identifies the instance of the connecting transport
            // segment
            auto bf_seg = bf_transport;
            // align bitfields and perform AND operation
            if (sa_total < 0) {
              bf_seg &= start_day >> static_cast<unsigned>(-1 * sa_total);
            } else {
              bf_seg &= start_day << static_cast<unsigned>(sa_total);
            }
            if (bf_seg.any()) {
              // enqueue if a matching bit is found
              enqueue(transport_idx, stop_idx, bf_seg, 0, nullptr);
              break;
            }
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

  // minimal travel time observed so far
  auto t_min = duration_t::max();

  // identifies instances of a transport segment that is the target of a
  // transfer
  bitfield bf_seg_to{};

  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (auto n{0U}; n != q_start_.size(); ++n) {
    // iterate trip segments in Q_n
    for (; q_cur_[n] < q_end_[n]; ++q_cur_[n]) {
      // the current transport segment
      auto const& tp_seg = q_[q_cur_[n]];
      // departure time at the start of the transport segment
      auto const dep_b = tt_.event_mam(
          tp_seg.transport_idx_, tp_seg.stop_idx_start_, event_type::kDep);
      // day index of the query
      auto const x = static_cast<int>(day_idx.v_);
      // day index of the current transport segment
      auto const y = bitfield_to_day_idx(tt_.bitfields_[tp_seg.bitfield_idx_]) +
                     num_midnights(dep_b);
      // departure time at start of current transport segment in minutes after
      // midnight on the day of the query
      auto const t_b = (y - x) * duration_t{1440} + time_of_day(dep_b);

      // check if target location is reached from current transport segment
      for (auto const& le : l_) {
        if (le.route_idx_ == tt_.transport_route_[tp_seg.transport_idx_] &&
            tp_seg.stop_idx_start_ < le.stop_idx_) {
          // the time it takes to travel on this transport segment
          auto const travel_time_seg =
              tt_.event_mam(tp_seg.transport_idx_, le.stop_idx_,
                            event_type::kArr) -
              dep_b;
          // the time at which the target location is reached by using the
          // current transport segment
          auto const t_cur = t_b + travel_time_seg + le.time_;
          if (t_cur < t_min) {
            t_min = t_cur;
            // TODO add journey
            // j_.add(...)
          }
        }
      }

      // the time it takes to travel to the next stop of the transport segment
      auto const travel_time_next =
          tt_.event_mam(tp_seg.transport_idx_, tp_seg.stop_idx_start_ + 1,
                        event_type::kArr) -
          dep_b;

      // transfer out of current transport segment?
      if (t_b + travel_time_next < t_min) {
        // iterate stops of the current transport segment
        for (auto stop_idx{tp_seg.stop_idx_start_ + 1};
             stop_idx <= tp_seg.stop_idx_end_; ++stop_idx) {
          auto const stop = timetable::stop{
              tt_.route_location_seq_
                  [tt_.transport_route_[tp_seg.transport_idx_]][stop_idx]};
          auto const ts_pair = tbp_.ts_.get_transfers(tp_seg.transport_idx_,
                                                      stop.location_idx());
          // check if there are transfers from this stop
          if (ts_pair.has_value()) {
            auto const transfers_start = ts_pair->first;
            auto const transfers_end = ts_pair->second;
            // iterate transfers from this stop
            for (auto transfer_idx = transfers_start;
                 transfer_idx != transfers_end; ++transfer_idx) {
              // the current transfer
              auto const& transfer_cur = tbp_.ts_[transfer_idx];
              // bitset specifying the instance of the transport segment
              auto const& bf_seg = tt_.bitfields_[tp_seg.bitfield_idx_];
              // bitset specifying the days on which the transfer is possible
              // from the current transport segment
              auto const& bf_transfer =
                  tt_.bitfields_[transfer_cur.bitfield_idx_];
              // compute the instance of transport segment that we are
              // transferring to
              if (transfer_cur.shift_amount_ < 0) {
                bf_seg_to = (bf_seg & bf_transfer) >>
                            static_cast<unsigned>(-transfer_cur.shift_amount_);
              } else {
                bf_seg_to =
                    (bf_seg & bf_transfer)
                    << static_cast<unsigned>(transfer_cur.shift_amount_);
              }
              // enqueue if transfer is possible
              if (bf_seg_to.any()) {
                enqueue(transfer_cur.transport_idx_to_, stop_idx, bf_seg_to,
                        n + 1, &tp_seg);
              }
            }
          }
        }
      }
    }
  }
}

void tb_query::reconstruct_journey(tb_query::transport_segment const& tp_seg,
                                   journey& j) {
  // build vector of all segments of the journey, last added is first segment
  std::vector<tb_query::transport_segment const*> segment_stack = {&tp_seg};
  while (segment_stack.back()->transferred_from_ != nullptr) {
    segment_stack.emplace_back(segment_stack.back()->transferred_from_);
  }

  // process segment stack in reverse order
  for (auto const& tp_seg_cur : std::ranges::reverse_view(segment_stack)) {
    auto const& route_idx = tt_.transport_route_[tp_seg_cur->transport_idx_];
    auto const location_idx_from =
        timetable::stop{
            tt_.route_location_seq_[route_idx][tp_seg_cur->stop_idx_start_]}
            .location_idx();
    auto const location_idx_to =
        timetable::stop{
            tt_.route_location_seq_[route_idx][tp_seg_cur->stop_idx_end_]}
            .location_idx();
    day_idx_t const day_idx{
        bitfield_to_day_idx(tt_.bitfields_[tp_seg_cur->bitfield_idx_])};
    auto const time_dep_from = tt_.to_unixtime(
        day_idx, tt_.event_mam(tp_seg_cur->transport_idx_,
                               tp_seg_cur->stop_idx_start_, event_type::kDep));
    auto const time_arr_to = tt_.to_unixtime(
        day_idx, tt_.event_mam(tp_seg_cur->transport_idx_,
                               tp_seg_cur->stop_idx_end_, event_type::kArr));
    journey::transport_enter_exit tee(
        /*TODO time table needs something like get_transport(transport_idx)*/);
    j.add(direction::kForward, location_idx_from, location_idx_to,
          time_dep_from, time_arr_to, tee);
  }
}
