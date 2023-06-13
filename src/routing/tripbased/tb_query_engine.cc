#include <ranges>

#include "nigiri/routing/tripbased/tb_query_engine.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query_engine::execute(unixtime_t const start_time,
                              std::uint8_t const max_transfers,
                              unixtime_t const worst_time_at_dest,
                              pareto_set<journey>& results) {

  auto const day_idx_mam_pair = tt_.day_idx_mam(state_.start_time_);
  // day index of start day
  auto const d = day_idx_mam_pair.first;
  // minutes after midnight on the start day
  auto const tau = day_idx_mam_pair.second;

  // fill Q_0
  auto create_q0_entry = [&tau, this, &d](footpath const& fp) {
    // arrival time after walking the footpath
    auto const tau_alpha = tau + fp.duration();
    // arrival time after walking the footpath
    auto const tau_alpha_delta = delta{tau_alpha};
    // shift amount due to walking the footpath
    auto const sigma_fp = day_idx_t{tau_alpha_delta.days()};
    // iterate routes at target stop of footpath
    for (auto const route_idx : tt_.location_routes_[fp.target()]) {
      // iterate stop sequence of route, skip last stop
      for (std::uint16_t i = 0U;
           i < tt_.route_location_seq_[route_idx].size() - 1; ++i) {
        auto const q =
            stop{tt_.route_location_seq_[route_idx][i]}.location_idx();
        if (q == fp.target()) {
          // shift amount due to waiting for connection
          day_idx_t sigma_w{0U};
          // departure times of this route at this q
          auto const event_times =
              tt_.event_times_at_stop(route_idx, i, event_type::kDep);
          // iterator to departure time of connecting transport at this
          // q
          auto tau_dep_t_i_delta = std::lower_bound(
              event_times.begin(), event_times.end(), tau_alpha_delta,
              [&](auto&& x, auto&& y) { return x.mam() < y.mam(); });
          // no departure found on the day of alpha
          if (tau_dep_t_i_delta == event_times.end()) {
            // start looking at the following day
            ++sigma_w;
            tau_dep_t_i_delta = event_times.begin();
          }
          // iterate departures until maximum waiting time is reached
          while (sigma_w <= state_.ts_.stats_.sigma_w_max_) {
            // shift amount due to travel time of transport
            auto const sigma_t = day_idx_t{tau_dep_t_i_delta->days()};
            // day index of the transport segment
            auto const d_seg = d + sigma_fp + sigma_w - sigma_t;
            // offset of connecting transport in route_transport_ranges
            auto const k =
                std::distance(event_times.begin(), tau_dep_t_i_delta);
            // transport_idx_t of the connecting transport
            auto const t =
                tt_.route_transport_ranges_[route_idx]
                                           [static_cast<std::size_t>(k)];
            // bitfield of the connecting transport
            auto const& beta_t = tt_.bitfields_[tt_.transport_traffic_days_[t]];

            if (beta_t.test(d_seg.v_)) {
              // enqueue segment if alpha matching bit is found
              state_.q_.enqueue(d_seg, t, i, 0U, TRANSFERRED_FROM_NULL);
              break;
            }
            // passing midnight?
            if (tau_dep_t_i_delta + 1 == event_times.end()) {
              ++sigma_w;
              // start with the earliest transport on the next day
              tau_dep_t_i_delta = event_times.begin();
            } else {
              ++tau_dep_t_i_delta;
            }
          }
        }
      }
    }
  };
  // virtual reflexive footpath
  create_q0_entry(footpath{state_.start_location_, duration_t{0U}});
  // iterate outgoing footpaths of source location
  for (auto const fp_q :
       tt_.locations_.footpaths_out_[state_.start_location_]) {
    create_q0_entry(fp_q);
  }

  // process all Q_n in ascending order, i.e., transport segments reached after
  // n transfers
  for (std::uint8_t n = 0U; n != state_.q_.start_.size() && n <= max_transfers;
       ++n) {
    // iterate trip segments in Q_n
    for (auto q_cur = state_.q_.start_[n]; q_cur != state_.q_.end_[n];
         ++q_cur) {
      // the current transport segment
      auto seg = state_.q_[q_cur];
      // departure time at the start of the transport segment
      auto const tau_dep_t_b_delta = tt_.event_mam(
          seg.get_transport_idx(), seg.stop_idx_start_, event_type::kDep);
      // the day index of the segment
      auto const d_seg = seg.get_transport_day(base_).v_;
      // departure time at start of current transport segment in minutes after
      // midnight on the day of this earliest arrival query
      auto const tau_d =
          duration_t{(d_seg +
                      static_cast<std::uint16_t>(tau_dep_t_b_delta.days()) -
                      d.v_) *
                     1440U} +
          duration_t{tau_dep_t_b_delta.mam()};

      // check if target location is reached from current transport segment
      for (auto const& le : state_.l_) {
        if (le.route_idx_ == tt_.transport_route_[seg.get_transport_idx()] &&
            seg.stop_idx_start_ < le.stop_idx_ &&
            le.stop_idx_ <= seg.stop_idx_end_) {
          // the time it takes to travel on this transport segment
          auto const travel_time_seg =
              tt_.event_mam(seg.get_transport_idx(), le.stop_idx_,
                            event_type::kArr) -
              tau_dep_t_b_delta;
          // the time at which the target location is reached by using the
          // current transport segment
          auto const t_cur = tt_.to_unixtime(
              d, tau_d + travel_time_seg.as_duration() + le.time_);
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
          tt_.event_mam(seg.get_transport_idx(), seg.stop_idx_start_ + 1,
                        event_type::kArr) -
          tau_dep_t_b_delta;

      // the unix time at the next stop of the transport segment
      auto const unix_time_next =
          tt_.to_unixtime(d, tau_d + travel_time_next.as_duration());

      // transfer out of current transport segment?
      if (unix_time_next < state_.t_min_[n] &&
          unix_time_next < worst_time_at_dest) {
        // iterate stops of the current transport segment
        for (auto i{seg.stop_idx_start_ + 1U}; i <= seg.stop_idx_end_; ++i) {
          // get transfers for this transport/stop
          auto const& transfers =
              state_.ts_.data_.at(seg.get_transport_idx().v_, i);
          // iterate transfers from this stop
          for (auto const& transfer_cur : transfers) {
            // bitset specifying the days on which the transfer is possible
            // from the current transport segment
            auto const& theta = tt_.bitfields_[transfer_cur.get_bitfield_idx()];
            // enqueue if transfer is possible
            if (theta.test(d_seg)) {
              // arrival time at start location of transfer
              auto const tau_arr_t_i_delta =
                  tt_.event_mam(seg.get_transport_idx(), i, event_type::kArr);
              // departure time at end location of transfer
              auto const tau_dep_u_j_delta =
                  tt_.event_mam(transfer_cur.get_transport_idx_to(),
                                transfer_cur.stop_idx_to_, event_type::kDep);

              auto const d_tr = seg.get_transport_day(base_) +
                                day_idx_t{tau_arr_t_i_delta.days()} -
                                day_idx_t{tau_dep_u_j_delta.days()} +
                                transfer_cur.get_passes_midnight();
              state_.q_.enqueue(d_tr, transfer_cur.get_transport_idx_to(),
                                transfer_cur.get_stop_idx_to(), n + 1U, q_cur);
            }
          }
        }
      }
    }
  }
}

void tb_query_engine::reconstruct(query const& q, journey& j) {

  auto const last_seg_match = find_last_seg(j);
  if (!last_seg_match.has_value()) {
    std::cerr << "Journey reconstruction failed: Could not find a matching "
                 "last segment\n";
    return;
  }

  auto cur_seg_q_idx = last_seg_match.value().first;
  auto const le_match = last_seg_match.value().second;

  // follow transferred_from_ back to the start of the journey
  while (cur_seg_q_idx != TRANSFERRED_FROM_NULL) {
    // the segment currently processed
    auto const tp_seg = state_.q_[cur_seg_q_idx];
    // the route index of the current segment
    auto const route_idx = tt_.transport_route_[tp_seg.get_transport_idx()];

    // the location index at the start of the segment
    auto const seg_start_idx =
        stop{tt_.route_location_seq_[route_idx][tp_seg.stop_idx_start_]}
            .location_idx();

    // transport time: departure time at the start of the segment
    auto const seg_start_delta = tt_.event_mam(
        tp_seg.get_transport_idx(), tp_seg.stop_idx_start_, event_type::kDep);

    // unix time: departure time at the start of the segment
    auto const unix_start = tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                            seg_start_delta.as_duration());
    // the stop index of the end of the segment
    auto stop_idx_end = tp_seg.stop_idx_end_;
    // arrival time at the end of the segment
    auto seg_end_delta = tt_.event_mam(tp_seg.get_transport_idx(), stop_idx_end,
                                       event_type::kArr);
    // unix time: arrival time at the end of the segment
    auto unix_end = tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                    seg_end_delta.as_duration());
    // the location index of the end of the segment
    auto seg_end_idx =
        stop{tt_.route_location_seq_[route_idx][stop_idx_end]}.location_idx();

    // add footpath
    if (j.legs_.empty()) {
      // add final footpath when handling the last segment
      stop_idx_end = le_match.stop_idx_;
      seg_end_delta = tt_.event_mam(tp_seg.get_transport_idx(), stop_idx_end,
                                    event_type::kArr);
      unix_end = tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                 seg_end_delta.as_duration());
      seg_end_idx =
          stop{tt_.route_location_seq_[route_idx][stop_idx_end]}.location_idx();

      if (q.dest_match_mode_ != location_match_mode::kIntermodal) {
        // station-to-station

        if (!is_dest_[j.dest_.v_]) {
          auto const reconstructed_dest = reconstruct_dest(le_match);
          if (!reconstructed_dest.has_value()) {
            std::cerr << "Journey reconstruction failed: Could not find a "
                         "matching destination\n";
            return;
          }
          j.dest_ = reconstructed_dest.value();
        }

        footpath fp{j.dest_, le_match.time_};
        auto const unix_fp_end =
            tt_.to_unixtime(tp_seg.get_transport_day(base_),
                            seg_end_delta.as_duration() + fp.duration());

        journey::leg l_fp{direction::kForward, seg_end_idx, j.dest_, unix_end,
                          unix_fp_end,         fp};
        j.add(std::move(l_fp));
      } else {
        // coordinate-to-coordinate

        // set destination location to END
        j.dest_ = location_idx_t{1U};

        // add offset leg for intermodal query

        // virtual reflexive outgoing footpath
        footpath const fp_reflexive{seg_end_idx, duration_t{0U}};
        auto MUMO_offset = find_MUMO(le_match, q, fp_reflexive);
        if (MUMO_offset.has_value()) {
          journey::leg l_mumo_end{
              direction::kForward, seg_end_idx,        j.dest_, unix_end,
              j.dest_time_,        MUMO_offset.value()};
          j.add(std::move(l_mumo_end));
        } else {
          // iterate outgoing footpaths of final location
          for (auto const fp : tt_.locations_.footpaths_out_[seg_end_idx]) {
            MUMO_offset = find_MUMO(le_match, q, fp);
            if (MUMO_offset.has_value()) {
              // reconstruct footpath leg to MUMO location and MUMO leg

              auto const unix_fp_end =
                  tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                  seg_end_delta.as_duration() + fp.duration());
              journey::leg l_fp{direction::kForward, seg_end_idx,
                                fp.target(),         unix_end,
                                unix_fp_end,         fp};
              journey::leg l_mumo_end{
                  direction::kForward, fp.target(),  j.dest_,
                  unix_fp_end,         j.dest_time_, MUMO_offset.value()};

              // add MUMO leg
              j.add(std::move(l_mumo_end));
              // add footpath to MUMO leg
              j.add(std::move(l_fp));
              break;
            }
          }
        }
      }
    } else {
      // rescan for transfer to next leg
      for (std::uint16_t stop_idx = tp_seg.stop_idx_start_ + 1U;
           stop_idx <= tp_seg.stop_idx_end_; ++stop_idx) {
        // get transfers for this transport/stop
        auto const& transfers =
            state_.ts_.data_.at(tp_seg.get_transport_idx().v_, stop_idx);
        // iterate transfers for this stop
        for (auto const& transfer_cur : transfers) {
          if (transfer_cur.get_transport_idx_to() ==
                  get<journey::transport_enter_exit>(j.legs_.back().uses_)
                      .t_.t_idx_ &&
              transfer_cur.stop_idx_to_ ==
                  get<journey::transport_enter_exit>(j.legs_.back().uses_)
                      .stop_range_.from_ &&
              tt_.bitfields_[transfer_cur.get_bitfield_idx()].test(
                  tp_seg.get_transport_day(base_).v_)) {
            // set end stop index of current segment according to found
            // transfer
            stop_idx_end = stop_idx;
            seg_end_delta = tt_.event_mam(tp_seg.get_transport_idx(),
                                          stop_idx_end, event_type::kArr);
            unix_end = tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                       seg_end_delta.as_duration());
            seg_end_idx = stop{tt_.route_location_seq_[route_idx][stop_idx_end]}
                              .location_idx();

            // create footpath journey leg for the transfer
            auto create_fp_leg = [this, &tp_seg, &seg_end_delta, &seg_end_idx,
                                  &j, &unix_end](footpath const& fp) {
              auto const unix_fp_end =
                  tt_.to_unixtime(tp_seg.get_transport_day(base_),
                                  seg_end_delta.as_duration() + fp.duration());

              journey::leg l_fp{direction::kForward,  seg_end_idx,
                                j.legs_.back().from_, unix_end,
                                unix_fp_end,          fp};
              j.add(std::move(l_fp));
            };

            // handle reflexive footpath
            if (seg_end_idx == j.legs_.back().from_) {
              footpath const reflexive_fp{
                  seg_end_idx, tt_.locations_.transfer_time_[seg_end_idx]};
              create_fp_leg(reflexive_fp);
            } else {
              // find footpath used
              for (auto const fp : tt_.locations_.footpaths_out_[seg_end_idx]) {
                // check if footpath reaches the start of the next journey
                // leg
                if (fp.target() == j.legs_.back().from_) {
                  create_fp_leg(fp);
                  break;
                }
              }
            }
            goto transfer_handled;
          }
        }
      }
    }

  transfer_handled:

    // add journey leg for this transport segment
    journey::transport_enter_exit tee{
        transport{tp_seg.get_transport_idx(), tp_seg.get_transport_day(base_)},
        tp_seg.stop_idx_start_, stop_idx_end};
    journey::leg l_tp{direction::kForward, seg_start_idx, seg_end_idx,
                      unix_start,          unix_end,      tee};
    j.add(std::move(l_tp));

    // first segment reached? -> add initial footpath if necessary
    if (tp_seg.transferred_from_ == TRANSFERRED_FROM_NULL &&
        q.start_match_mode_ != location_match_mode::kIntermodal &&
        !is_start(q, seg_start_idx)) {

      auto const start_offset = find_closest_start(q, seg_start_idx);
      if (!start_offset.has_value()) {
        std::cerr << "Journey reconstruction failed: Could not find a "
                     "matching start location\n";
        return;
      }

      for (auto const& fp :
           tt_.locations_.footpaths_out_[start_offset->target()]) {
        if (fp.target() == seg_start_idx) {
          // transport time: start of footpath
          auto const time_fp_start =
              seg_start_delta.as_duration() - fp.duration();
          // unix time: start of footpath
          auto const unix_fp_start =
              tt_.to_unixtime(tp_seg.get_transport_day(base_), time_fp_start);
          journey::leg l_fp{direction::kForward, start_offset->target(),
                            seg_start_idx,       unix_fp_start,
                            unix_start,          fp};
          j.add(std::move(l_fp));
        }
      }
    }

    // prep next iteration
    cur_seg_q_idx = tp_seg.transferred_from_;
  }

  if (q.start_match_mode_ == location_match_mode::kIntermodal) {
    // add MUMO from START to first journey leg
    for (auto const& os : q.start_) {
      if (os.target() == j.legs_.back().from_) {
        unixtime_t const mumo_start_unix{j.legs_.back().dep_time_ -
                                         os.duration()};
        journey::leg l_mumo_start{
            direction::kForward, location_idx_t{0U},       os.target(),
            mumo_start_unix,     j.legs_.back().dep_time_, os};
        j.add(std::move(l_mumo_start));
        break;
      }
    }
  }

  // reverse order of journey legs
  std::reverse(j.legs_.begin(), j.legs_.end());
}

std::optional<std::pair<std::uint32_t, l_entry>> tb_query_engine::find_last_seg(
    journey const& j) {
  for (auto q_cur = state_.q_.start_[j.transfers_];
       q_cur != state_.q_.end_[j.transfers_]; ++q_cur) {
    // the transport segment currently processed
    auto const& tp_seg = state_.q_[q_cur];

    // the route index of the current segment
    auto const route_idx = tt_.transport_route_[tp_seg.get_transport_idx()];

    // find matching entry in l_
    for (auto const& le : state_.l_) {
      if (le.route_idx_ == route_idx && tp_seg.stop_idx_start_ < le.stop_idx_ &&
          le.stop_idx_ <= tp_seg.stop_idx_end_) {
        // transport day of the segment
        auto const transport_day = tp_seg.get_transport_day(base_);
        // transport time at destination
        auto const transport_time =
            tt_.event_mam(tp_seg.get_transport_idx(), le.stop_idx_,
                          event_type::kArr)
                .as_duration() +
            le.time_;
        // unix_time of segment at destination
        auto const seg_unix_dest =
            tt_.to_unixtime(transport_day, transport_time);
        // check if time at destination matches
        if (seg_unix_dest == j.dest_time_) {
          return std::pair<std::uint32_t, l_entry>{q_cur, le};
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<nigiri::routing::offset> tb_query_engine::find_MUMO(
    l_entry const& le, query const& q, footpath const& fp) {
  for (auto const& os : q.destination_) {
    if (os.target_ == fp.target() &&
        fp.duration() + duration_t{dist_to_dest_[fp.target_]} == le.time_) {
      return os;
    }
  }
  return std::nullopt;
};

bool tb_query_engine::is_start(query const& q,
                               location_idx_t const location_idx) {
  for (auto const& os : q.start_) {
    if (os.target() == location_idx && os.duration() == duration_t{0U}) {
      return true;
    }
  }
  return false;
}

std::optional<nigiri::routing::offset> tb_query_engine::find_closest_start(
    query const& q, location_idx_t const location_idx) {
  auto min = duration_t::max();
  std::optional<nigiri::routing::offset> result = std::nullopt;
  for (auto const& os : q.start_) {
    for (auto const fp : tt_.locations_.footpaths_out_[os.target()]) {
      if (fp.target() == location_idx && fp.duration() < min) {
        min = fp.duration();
        result = os;
      }
    }
  }
  return result;
}

std::optional<location_idx_t> tb_query_engine::reconstruct_dest(
    l_entry const& le) {
  auto const le_location_idx =
      stop{tt_.route_location_seq_[le.route_idx_][le.stop_idx_]}.location_idx();
  for (auto const& fp : tt_.locations_.footpaths_out_[le_location_idx]) {
    if (is_dest_[fp.target().v_] && fp.duration() == le.time_) {
      return fp.target();
    }
  }
  return std::nullopt;
}