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
      auto const bf_cur = tbp_.tt_.bitfields_[r_cur->bitfield_idx_] & ~bf_new;
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
      bf_new |= tbp_.tt_.bitfields_[r_cur->bitfield_idx_];
      if (overwrite_spot == r_.end()) {
        overwrite_spot = r_cur;
        ++r_cur;
      } else {
        r_.erase(r_cur);
      }
    } else {
      // new stop index is worse than current
      bf_new &= ~tbp_.tt_.bitfields_[r_cur->bitfield_idx_];
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
    if ((tbp_.tt_.bitfields_[r_cur->bitfield_idx_] & bf_query).any()) {
      return r_cur->stop_idx_;
    }
    ++r_cur;
  }

  // no entry for this transport_idx
  // return stop index of final stop of the transport
  return tbp_.tt_.route_location_seq_[tbp_.tt_.transport_route_[transport_idx]]
             .size() -
         1;
}

void tb_query::enqueue(const transport_idx_t transport_idx,
                       const unsigned int stop_idx,
                       bitfield const& bf,
                       const unsigned int n) {
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
                    tbp_.get_or_create_bfi(bf));
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
    auto const route_idx = tbp_.tt_.transport_route_[transport_idx];
    for (auto const transport_idx_it :
         tbp_.tt_.route_transport_ranges_[route_idx]) {

      // set the bit of the day of the instance to false if the current
      // transport is earlier than the newly enqueued
      auto const mam_u =
          tbp_.tt_.event_mam(transport_idx_it, 0U, event_type::kDep);
      auto const mam_t =
          tbp_.tt_.event_mam(transport_idx, 0U, event_type::kDep);
      if (mam_u < mam_t) {
        bf_new.set(k, false);
      } else {
        bf_new.set(k, true);
      }

      r_update(transport_idx_it, stop_idx, bf_new);
    }
  }
}

constexpr unsigned day_idx(bitfield const& bf) {
  assert(bf.count() == 1);
  auto i{0U};
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

  // holds source location
  auto const offset_source = query.start_[0];
  // holds target location
  auto const offset_target = query.destinations_[0][0];

  // departure time
  auto const start_unixtime = cista::get_if<unixtime_t>(query.start_time_);
  assert(start_unixtime);
  auto const day_idx_mam_pair = tbp_.tt_.day_idx_mam(*start_unixtime);
  auto const day_idx = day_idx_mam_pair.first;
  // minutes after midnight on the start day
  auto const start_mam = day_idx_mam_pair.second;
  // bit set identifying the start day
  bitfield start_day{};
  start_day.set(day_idx.v_);

  // fill l_
  // iterate incoming footpaths of target location
  for (auto const fp :
       tbp_.tt_.locations_.footpaths_in_[offset_target.target_]) {
    auto const delta_tau =
        offset_target.target_ == fp.target_ ? duration_t{0U} : fp.duration_;
    // iterate routes serving source of footpath
    for (auto const route_idx : tbp_.tt_.location_routes_[fp.target_]) {
      // iterate stop sequence of route
      for (auto stop_idx{0U};
           stop_idx < tbp_.tt_.route_location_seq_[route_idx].size();
           ++stop_idx) {
        auto const stop =
            timetable::stop{tbp_.tt_.route_location_seq_[route_idx][stop_idx]};
        if (stop.location_idx() == fp.target_) {
          l_.emplace_back(route_idx, stop_idx, delta_tau);
        }
      }
    }
  }

  // fill Q_0
  // iterate outgoing footpaths of source location
  for (auto const fp :
       tbp_.tt_.locations_.footpaths_out_[offset_source.target_]) {
    // arrival time after walking the footpath
    auto const t_a = start_mam + fp.duration_;
    // shift amount due to walking the footpath
    auto const sa_fp = num_midnights(t_a);
    // time of day after walking the footpath
    auto const a = time_of_day(t_a);
    // iterate routes at target stop of footpath
    for (auto const route_idx : tbp_.tt_.location_routes_[fp.target_]) {
      // iterate stop sequence of route, skip last stop
      for (auto stop_idx{0U};
           stop_idx < tbp_.tt_.route_location_seq_[route_idx].size() - 1;
           ++stop_idx) {
        auto const stop =
            timetable::stop{tbp_.tt_.route_location_seq_[route_idx][stop_idx]};
        if (stop.location_idx() == fp.target_) {
          // shift amount due to waiting for connection
          int sa_w = 0;
          // departure times of this route at this stop
          auto const event_times = tbp_.tt_.event_times_at_stop(
              route_idx, stop_idx, event_type::kDep);
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
                tbp_.tt_.route_transport_ranges_[route_idx]
                                                [static_cast<std::size_t>(
                                                    offset_transport)];
            // bitfield of the connecting transport
            auto const bf_transport =
                tbp_.tt_.bitfields_
                    [tbp_.tt_.transport_traffic_days_[transport_idx]];
            // bitfield that identifies the instance of the connecting transport
            auto bf_transport_seg = bf_transport;
            // align bitfields and perform AND operation
            if (sa_total < 0) {
              bf_transport_seg &=
                  start_day >> static_cast<unsigned>(-1 * sa_total);
            } else {
              bf_transport_seg &= start_day << static_cast<unsigned>(sa_total);
            }
            if (bf_transport_seg.any()) {
              // enqueue if a matching bit is found
              enqueue(transport_idx, stop_idx, bf_transport_seg, 0);
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
  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (auto n{0U}; n != q_start_.size(); ++n) {
    // iterate trip segments in Q_n
    for (; q_cur_[n] < q_end_[n]; ++q_cur_[n]) {
    }
  }
}
