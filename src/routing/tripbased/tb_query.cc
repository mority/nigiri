#include <ranges>

#include "nigiri/routing/tripbased/tb_query.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query::execute(unixtime_t const start_time,
                       std::uint8_t const max_transfers,
                       unixtime_t const worst_time_at_dest,
                       pareto_set<journey>& results) {

  auto const day_idx_mam_pair = tt_.day_idx_mam(start_time);
  // day index of start day
  auto const day_idx = day_idx_mam_pair.first;
  // minutes after midnight on the start day
  auto const start_mam = day_idx_mam_pair.second;

  // fill Q_0
  auto create_q0_entry = [&start_mam, this, &day_idx](footpath const& fp) {
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
        auto const location_idx =
            stop{tt_.route_location_seq_[route_idx][stop_idx]}.location_idx();
        if (location_idx == fp.target_) {
          // shift amount due to waiting for connection
          day_idx_t sa_w{0U};
          // departure times of this route at this location_idx
          auto const event_times =
              tt_.event_times_at_stop(route_idx, stop_idx, event_type::kDep);
          // iterator to departure time of connecting transport at this
          // location_idx
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
          while (sa_w <= state_.tbp_.sa_w_max_) {
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
              state_.q_.enqueue(day_idx_seg, transport_idx, stop_idx, 0U,
                                TRANSFERRED_FROM_NULL);
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
  };
  // virtual reflexive footpath
  create_q0_entry(footpath{state_.start_location_, duration_t{0U}});
  // iterate outgoing footpaths of source location
  for (auto const fp : tt_.locations_.footpaths_out_[state_.start_location_]) {
    create_q0_entry(fp);
  }

  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (std::uint8_t n = 0U; n != state_.q_.start_.size() && n <= max_transfers;
       ++n) {
    // iterate trip segments in Q_n
    for (auto q_cur = state_.q_.start_[n]; q_cur != state_.q_.end_[n];
         ++q_cur) {
      // the current transport segment
      auto tp_seg = state_.q_[q_cur];
      // departure time at the start of the transport segment
      auto const tp_seg_dep = tt_.event_mam(
          tp_seg.get_transport_idx(), tp_seg.stop_idx_start_, event_type::kDep);
      // departure time at start of current transport segment in minutes after
      // midnight on the day of this earliest arrival query
      auto const dep_query =
          duration_t{(tp_seg.get_transport_day(base_).v_ +
                      num_midnights(tp_seg_dep).v_ - day_idx.v_) *
                     1440U} +
          time_of_day(tp_seg_dep);

      // check if target location is reached from current transport segment
      for (auto const& le : state_.l_) {
        if (le.route_idx_ == tt_.transport_route_[tp_seg.get_transport_idx()] &&
            tp_seg.stop_idx_start_ < le.stop_idx_ &&
            le.stop_idx_ <= tp_seg.stop_idx_end_) {
          // the time it takes to travel on this transport segment
          auto const travel_time_seg =
              tt_.event_mam(tp_seg.get_transport_idx(), le.stop_idx_,
                            event_type::kArr) -
              tp_seg_dep;
          // the time at which the target location is reached by using the
          // current transport segment
          auto const t_cur =
              tt_.to_unixtime(day_idx, dep_query + travel_time_seg + le.time_);
          if (t_cur < state_.t_min_[n]) {
            state_.t_min_[n] = t_cur;
            // add journey without reconstructing yet
            journey j{};
            j.start_time_ = start_time;
            j.dest_time_ = t_cur;
            j.dest_ = stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}
                          .location_idx();
            j.transfers_ = n;
            // add journey to pareto set (removes dominated entries)
            results.add(std::move(j));
          }
        }
      }

      // the time it takes to travel to the next stop of the transport segment
      auto const travel_time_next =
          tt_.event_mam(tp_seg.get_transport_idx(), tp_seg.stop_idx_start_ + 1,
                        event_type::kArr) -
          tp_seg_dep;

      // the unix time at the next stop of the transport segment
      auto const unix_time_next =
          tt_.to_unixtime(day_idx, dep_query + travel_time_next);

      // transfer out of current transport segment?
      if (unix_time_next < state_.t_min_[n] &&
          unix_time_next < worst_time_at_dest) {
        // iterate stops of the current transport segment
        for (auto stop_idx{tp_seg.stop_idx_start_ + 1U};
             stop_idx <= tp_seg.stop_idx_end_; ++stop_idx) {
          // get transfers for this transport/stop
          auto const& transfers =
              state_.tbp_.ts_.at(tp_seg.get_transport_idx().v_, stop_idx);
          // iterate transfers from this stop
          for (auto const& transfer_cur : transfers) {
            // bitset specifying the days on which the transfer is possible
            // from the current transport segment
            auto const& bf_transfer =
                tt_.bitfields_[transfer_cur.get_bitfield_idx()];
            // enqueue if transfer is possible
            if (bf_transfer.test(tp_seg.get_transport_day(base_).v_)) {
              // arrival time at start location of transfer
              auto const time_arr = tt_.event_mam(tp_seg.get_transport_idx(),
                                                  stop_idx, event_type::kArr);
              // departure time at end location of transfer
              auto const time_dep =
                  tt_.event_mam(transfer_cur.get_transport_idx_to(),
                                transfer_cur.stop_idx_to_, event_type::kDep);

              auto const day_index_transfer =
                  tp_seg.get_transport_day(base_) + num_midnights(time_arr) -
                  num_midnights(time_dep) + transfer_cur.get_passes_midnight();
              state_.q_.enqueue(
                  day_index_transfer, transfer_cur.get_transport_idx_to(),
                  static_cast<std::uint16_t>(transfer_cur.stop_idx_to_), n + 1U,
                  q_cur);
            }
          }
        }
      }
    }
  }
}

void tb_query::reconstruct(query const& q, journey& j) {

  // find last segment in Q_n
  for (auto q_cur = state_.q_.start_[j.transfers_];
       q_cur != state_.q_.end_[j.transfers_]; ++q_cur) {

    // the transport segment currently processed
    auto const* tp_seg = &(state_.q_[q_cur]);

    // the route index of the current segment
    auto const route_idx = tt_.transport_route_[tp_seg->get_transport_idx()];

    // the entry in l_ that this segment reaches
    std::optional<l_entry> le_match = std::nullopt;
    // find matching entry in l_
    for (auto const& le : state_.l_) {
      if (le.route_idx_ == route_idx &&
          tp_seg->stop_idx_start_ < le.stop_idx_ &&
          le.stop_idx_ <= tp_seg->stop_idx_end_) {
        // transport day of the segment
        auto const transport_day = tp_seg->get_transport_day(base_);
        // transport time at destination
        auto const transport_time =
            tt_.event_mam(tp_seg->get_transport_idx(), le.stop_idx_,
                          event_type::kArr) +
            le_match.value().time_;
        // unix_time of segment at destination
        auto const seg_unix_dest =
            tt_.to_unixtime(transport_day, transport_time);
        // check if time at destination matches
        if (seg_unix_dest == j.dest_time_) {
          le_match = le;
          break;
        }
      }
    }

    // if the current segment reaches the destination at the correct time
    if (le_match.has_value()) {

      // follow transferred_from_ back to the start of the journey
      while (true) {
        // the location index of the start of the segment
        auto const location_idx_start =
            stop{tt_.route_location_seq_[route_idx][tp_seg->stop_idx_start_]}
                .location_idx();

        // transport time: departure time at the start of the segment
        auto const time_dep =
            tt_.event_mam(tp_seg->get_transport_idx(), tp_seg->stop_idx_start_,
                          event_type::kDep);

        // unix time: departure time at the start of the segment
        auto const unix_start =
            tt_.to_unixtime(tp_seg->get_transport_day(base_), time_dep);
        // the stop index of the end of the segment
        auto stop_idx_end = tp_seg->stop_idx_end_;
        // arrival time at the end of the segment
        auto time_arr_end = tt_.event_mam(tp_seg->get_transport_idx(),
                                          stop_idx_end, event_type::kArr);
        // unix time: arrival time at the end of the segment
        auto unix_end =
            tt_.to_unixtime(tp_seg->get_transport_day(base_), time_arr_end);
        // the location index of the end of the segment
        auto location_idx_end =
            stop{tt_.route_location_seq_[route_idx][stop_idx_end]}
                .location_idx();

        // reconstruct transfer to following segment, i.e., the back of
        // j.legs_
        if (!j.legs_.empty()) {
          // rescan for transfer to next leg
          for (std::uint16_t stop_idx = tp_seg->stop_idx_start_ + 1U;
               stop_idx <= tp_seg->stop_idx_end_; ++stop_idx) {
            // get transfers for this transport/stop
            auto const& transfers =
                state_.tbp_.ts_.at(tp_seg->get_transport_idx().v_, stop_idx);
            // iterate transfers for this stop
            for (auto const& transfer_cur : transfers) {
              if (transfer_cur.get_transport_idx_to() ==
                      get<journey::transport_enter_exit>(j.legs_.back().uses_)
                          .t_.t_idx_ &&
                  transfer_cur.stop_idx_to_ ==
                      get<journey::transport_enter_exit>(j.legs_.back().uses_)
                          .stop_range_.from_ &&
                  tt_.bitfields_[transfer_cur.get_bitfield_idx()].test(
                      tp_seg->get_transport_day(base_).v_)) {
                // set end stop index of current segment according to found
                // transfer
                stop_idx_end = stop_idx;
                time_arr_end = tt_.event_mam(tp_seg->get_transport_idx(),
                                             stop_idx_end, event_type::kArr);
                unix_end = tt_.to_unixtime(tp_seg->get_transport_day(base_),
                                           time_arr_end);
                location_idx_end =
                    stop{tt_.route_location_seq_[route_idx][stop_idx_end]}
                        .location_idx();

                // create footpath journey leg for the transfer
                auto create_fp_leg = [this, &tp_seg, &time_arr_end,
                                      &location_idx_end, &j,
                                      &unix_end](footpath const& fp) {
                  auto const unix_fp_end =
                      tt_.to_unixtime(tp_seg->get_transport_day(base_),
                                      time_arr_end + fp.duration_);

                  journey::leg l_fp{direction::kForward,  location_idx_end,
                                    j.legs_.back().from_, unix_end,
                                    unix_fp_end,          fp};
                  j.add(std::move(l_fp));
                };

                // handle reflexive footpath
                if (location_idx_end == j.legs_.back().from_) {
                  footpath const reflexive_fp{
                      location_idx_end,
                      tt_.locations_.transfer_time_[location_idx_end]};
                  create_fp_leg(reflexive_fp);
                  goto transfer_handled;
                } else {
                  // find footpath used
                  for (auto const fp :
                       tt_.locations_.footpaths_out_[location_idx_end]) {
                    // check if footpath reaches the start of the next journey
                    // leg
                    if (fp.target_ == j.legs_.back().from_) {
                      create_fp_leg(fp);
                      goto transfer_handled;
                    }
                  }
                }
              }
            }
          }
        } else {
          // add final footpath when handling the last segment
          stop_idx_end = le_match->stop_idx_;
          time_arr_end = tt_.event_mam(tp_seg->get_transport_idx(),
                                       stop_idx_end, event_type::kArr);
          unix_end =
              tt_.to_unixtime(tp_seg->get_transport_day(base_), time_arr_end);
          location_idx_end =
              stop{tt_.route_location_seq_[route_idx][stop_idx_end]}
                  .location_idx();

          if (q.dest_match_mode_ != location_match_mode::kIntermodal) {
            footpath fp{j.dest_, le_match.value().time_};
            auto const unix_fp_end = tt_.to_unixtime(
                tp_seg->get_transport_day(base_), time_arr_end + fp.duration_);

            journey::leg l_fp{direction::kForward,
                              location_idx_end,
                              j.dest_,
                              unix_end,
                              unix_fp_end,
                              fp};
            j.add(std::move(l_fp));
          }
        }

      transfer_handled:

        // add journey leg for this transport segment
        journey::transport_enter_exit tee{
            transport{tp_seg->get_transport_idx(),
                      tp_seg->get_transport_day(base_)},
            tp_seg->stop_idx_start_, stop_idx_end};
        journey::leg l_tp{
            direction::kForward, location_idx_start, location_idx_end,
            unix_start,          unix_end,           tee};
        j.add(std::move(l_tp));

        // first segment reached?
        if (tp_seg->transferred_from_ == TRANSFERRED_FROM_NULL) {
          // add initial footpath if necessary
          if (q.start_match_mode_ != location_match_mode::kIntermodal &&
              location_idx_start != state_.start_location_) {
            for (auto const& fp :
                 tt_.locations_.footpaths_out_[state_.start_location_]) {
              if (fp.target_ == location_idx_start) {
                // transport time: start of footpath
                auto const time_fp_start = time_dep - fp.duration_;
                // unix time: start of footpath
                auto const unix_fp_start = tt_.to_unixtime(
                    tp_seg->get_transport_day(base_), time_fp_start);
                journey::leg l_fp{direction::kForward, state_.start_location_,
                                  location_idx_start,  unix_fp_start,
                                  unix_start,          fp};
                j.add(std::move(l_fp));
              }
            }
          }
          goto segments_handled;
        }

        // prep next iteration
        tp_seg = &state_.q_[tp_seg->transferred_from_];
      }
    }
  }

segments_handled:

  // reverse order of journey legs
  std::reverse(j.legs_.begin(), j.legs_.end());
}