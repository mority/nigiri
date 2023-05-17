#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

bitfield_idx_t tb_preprocessing::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

bool tb_preprocessing::earliest_times::update(location_idx_t location_idx,
                                              duration_t time_new,
                                              bitfield const& bf) {
  // bitfield of the update
  bitfield bf_new = bf;

  // iterator to first entry that has no active days, overwrite with new entry
  // instead
  unsigned overwrite_spot =
      static_cast<unsigned>(location_idx_times_[location_idx].size());

  // iterate entries for this location
  for (auto i{0U}; i < location_idx_times_[location_idx].size(); ++i) {
    // the earliest time entry we are currently examining
    auto& et_cur = location_idx_times_[location_idx][i];
    if (bf_new.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return false;
    } else if (tbp_.tt_.bitfields_[et_cur.bf_idx_].any()) {
      if (time_new < et_cur.time_) {
        // new time is better than current time, update bit set of current
        // time
        et_cur.bf_idx_ = tbp_.get_or_create_bfi(
            tbp_.tt_.bitfields_[et_cur.bf_idx_] & ~bf_new);
      } else if (time_new == et_cur.time_) {
        // entry for this time already exists
        if ((bf_new & ~tbp_.tt_.bitfields_[et_cur.bf_idx_]).none()) {
          // all bits of bf_new already covered
          return false;
        } else {
          // new entry absorbs old entry
          bf_new |= tbp_.tt_.bitfields_[et_cur.bf_idx_];
          et_cur.bf_idx_ = tbp_.get_or_create_bfi(bitfield{"0"});
        }
      } else {
        // new time is worse than current time, update bit set of new time
        bf_new &= ~tbp_.tt_.bitfields_[et_cur.bf_idx_];
      }
    }
    // we remember the first invalid entry as an overwrite spot for the new
    // entry
    if (overwrite_spot == location_idx_times_[location_idx].size() &&
        tbp_.tt_.bitfields_[et_cur.bf_idx_].none()) {
      overwrite_spot = i;
    }
  }

  if (bf_new.any()) {
    if (overwrite_spot == location_idx_times_[location_idx].size()) {
      location_idx_times_[location_idx].emplace_back(
          time_new, tbp_.get_or_create_bfi(bf_new));
    } else {
      location_idx_times_[location_idx][overwrite_spot].time_ = time_new;
      location_idx_times_[location_idx][overwrite_spot].bf_idx_ =
          tbp_.get_or_create_bfi(bf_new);
    }
    return true;
  } else {
    return false;
  }
}

void tb_preprocessing::build_transfer_set(bool uturn_removal, bool reduction) {

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for (transport_idx_t tpi_from{0U};
       tpi_from != tt_.transport_traffic_days_.size(); ++tpi_from) {

    if (reduction) {
      // clear earliest times
      ets_arr_.clear();
      ets_ch_.clear();
    }

    // ri from: route index from
    auto const ri_from = tt_.transport_route_[tpi_from];

    // iterate over stops of transport (skip first stop)
    auto const stops_from = tt_.route_location_seq_[ri_from];

    // si_from: stop index from
    auto si_from = reduction ? stops_from.size() - 1 : 1U;
    // reversed iteration direction for transfer reduction
    auto const si_limit = reduction ? 0U : stops_from.size();
    auto const si_step = [&si_from, reduction]() {
      if (reduction) {
        --si_from;
      } else {
        ++si_from;
      }
    };

    for (; si_from != si_limit; si_step()) {

      // li_from: location index from
      auto const li_from = timetable::stop{stops_from[si_from]}.location_idx();

      auto const t_arr_from =
          tt_.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_tp_from: shift amount transport from
      auto const sa_tp_from = num_midnights(t_arr_from);

      auto const& bf_tp_from =
          tt_.bitfields_[tt_.transport_traffic_days_[tpi_from]];

      if (reduction) {
        // init the earliest times data structure
        ets_arr_.update(li_from, t_arr_from, bf_tp_from);
        for (auto const& fp : tt_.locations_.footpaths_out_[li_from]) {
          ets_arr_.update(fp.target_, t_arr_from + fp.duration_, bf_tp_from);
          ets_ch_.update(fp.target_, t_arr_from + fp.duration_, bf_tp_from);
        }
      }

      auto handle_fp = [&t_arr_from, &sa_tp_from, this, &bf_tp_from, &tpi_from,
                        &si_from, &ri_from, &uturn_removal,
                        &reduction](footpath const& fp) {
        // li_to: location index of destination of footpath
        auto const li_to = fp.target_;

        // ta: arrival time at stop_to in relation to transport_from
        auto const ta = t_arr_from + fp.duration_;

        // sa_fp: shift amount footpath
        auto const sa_fp = num_midnights(ta) - sa_tp_from;

        // a: time of day when arriving at stop_to
        auto const a = time_of_day(ta);

        // iterate over lines serving stop_to
        auto const routes_at_stop_to = tt_.location_routes_[li_to];

        // ri_to: route index to
        for (auto const ri_to : routes_at_stop_to) {

          // ri_to might visit stop multiple times, skip if stop_to is the last
          // stop in the stop sequence of ri_to si_to: stop index to
          for (unsigned si_to = 0U;
               si_to < tt_.route_location_seq_[ri_to].size() - 1; ++si_to) {

            auto const stop_to_cur =
                timetable::stop{tt_.route_location_seq_[ri_to][si_to]};

            // li_from: location index from
            auto const li_to_cur = stop_to_cur.location_idx();

            if (li_to_cur == li_to) {

              // sa_w: shift amount due to waiting for connection
              day_idx_t sa_w{0U};

              // departure times of transports of route ri_to at stop si_to
              auto const event_times =
                  tt_.event_times_at_stop(ri_to, si_to, event_type::kDep);

              // find first departure at or after a
              // departure time of current transport_to
              auto dep_it =
                  std::lower_bound(event_times.begin(), event_times.end(), a,
                                   [&](auto&& x, auto&& y) {
                                     return time_of_day(x) < time_of_day(y);
                                   });

              // no departure on this day at or after a
              if (dep_it == event_times.end()) {
                ++sa_w;  // start looking on the following day
                dep_it = event_times.begin();  // with the earliest transport
              }

              // omega: days of transport_from that still require connection
              bitfield omega = bf_tp_from;

              // check if any bit in omega is set to 1 and maximum waiting time
              // not exceeded
              while (omega.any() && sa_w <= sa_w_max_) {

                // departure time of current transport in relation to time a
                auto const dep_cur = *dep_it + duration_t{sa_w.v_ * 1440};

                // offset from begin of tp_to interval
                auto const tp_to_offset =
                    std::distance(event_times.begin(), dep_it);

                // transport index of transport that we transfer to
                auto const tpi_to =
                    tt_.route_transport_ranges_[ri_to][static_cast<std::size_t>(
                        tp_to_offset)];

                // check conditions for required transfer
                // 1. different route OR
                // 2. earlier stop    OR
                // 3. same route but tpi_to is earlier than tpi_from
                auto const req =
                    ri_from != ri_to || si_to < si_from ||
                    (tpi_to != tpi_from &&
                     (dep_cur -
                          (tt_.event_mam(tpi_to, si_to, event_type::kDep) -
                           tt_.event_mam(tpi_to, si_from, event_type::kArr)) <=
                      a - fp.duration_));

                if (req) {
                  // shift amount due to number of times transport_to passed
                  // midnight
                  auto const sa_tp_to = num_midnights(*dep_it);

                  // total shift amount
                  auto const sa_total =
                      static_cast<int>(sa_tp_to.v_) -
                      static_cast<int>(sa_tp_from.v_ + sa_fp.v_ + sa_w.v_);

                  // bitfield transport to
                  auto const& bf_tp_to =
                      tt_.bitfields_[tt_.transport_traffic_days_[tpi_to]];

                  // bitfield transfer from
                  auto bf_tf_from = omega;

                  // align bitfields and perform AND
                  if (sa_total < 0) {
                    bf_tf_from &=
                        bf_tp_to >> static_cast<unsigned>(-1 * sa_total);
                  } else {
                    bf_tf_from &= bf_tp_to << static_cast<unsigned>(sa_total);
                  }

                  // check for match
                  if (bf_tf_from.any()) {

                    // remove days that are covered by this transfer from
                    // omega
                    omega &= ~bf_tf_from;

                    auto const check_uturn = [&si_to, this, &ri_to, &si_from,
                                              &ri_from, &dep_cur, &tpi_to, &a,
                                              &fp, &tpi_from]() {
                      // check if next stop for tpi_to and previous stop for
                      // tpi_from exists
                      if (si_to + 1 < tt_.route_location_seq_[ri_to].size() &&
                          si_from - 1 > 0) {
                        // check if next stop of tpi_to is the previous stop of
                        // tpi_from
                        auto const stop_to_next = timetable::stop{
                            tt_.route_location_seq_[ri_to][si_to + 1]};
                        auto const stop_from_prev = timetable::stop{
                            tt_.route_location_seq_[ri_from][si_from - 1]};
                        if (stop_to_next.location_idx() ==
                            stop_from_prev.location_idx()) {
                          // check if tpi_to is already reachable at the
                          // previous stop of tpi_from
                          auto const dep_to_next =
                              dep_cur +
                              (tt_.event_mam(tpi_to, si_to + 1,
                                             event_type::kDep) -
                               tt_.event_mam(tpi_to, si_to, event_type::kDep));
                          auto const arr_from_prev =
                              a - fp.duration_ -
                              (tt_.event_mam(tpi_from, si_from,
                                             event_type::kArr) -
                               tt_.event_mam(tpi_from, si_from,
                                             event_type::kArr));
                          auto const transfer_time_from_prev =
                              tt_.locations_.transfer_time_
                                  [stop_from_prev.location_idx()];
                          return arr_from_prev + transfer_time_from_prev <=
                                 dep_to_next;
                        }
                      }
                      return false;
                    };

                    // perform uturn_check if uturn_removal flag is set
                    auto const is_uturn = uturn_removal ? check_uturn() : false;

                    if (!is_uturn) {

                      auto const check_impr = [&ta, &dep_cur, &a, &si_to, this,
                                               &ri_to, &tpi_to, &dep_it,
                                               &bf_tf_from]() {
                        bool impr = false;
                        auto const t_dep_to = ta + (dep_cur - a);
                        for (auto si_to_later = si_to + 1;
                             si_to_later !=
                             tt_.route_location_seq_[ri_to].size();
                             ++si_to_later) {
                          auto const t_arr_to_later =
                              t_dep_to + (tt_.event_mam(tpi_to, si_to_later,
                                                        event_type::kArr) -
                                          *dep_it);
                          auto const li_to_later =
                              timetable::stop{
                                  tt_.route_location_seq_[ri_to][si_to_later]}
                                  .location_idx();
                          auto const ets_arr_res = ets_arr_.update(
                              li_to_later, t_arr_to_later, bf_tf_from);
                          impr = impr || ets_arr_res;
                          for (auto const& fp_later :
                               tt_.locations_.footpaths_out_[li_to_later]) {
                            auto const eta =
                                t_arr_to_later + fp_later.duration_;
                            auto const ets_arr_fp_res = ets_arr_.update(
                                fp_later.target_, eta, bf_tf_from);
                            auto const ets_ch_fp_res = ets_ch_.update(
                                fp_later.target_, eta, bf_tf_from);
                            impr = impr || ets_arr_fp_res || ets_ch_fp_res;
                          }
                        }
                        return impr;
                      };

                      auto const is_impr = reduction ? check_impr() : true;

                      if (is_impr) {
                        // construct and add transfer to transfer set
                        auto const bfi_from = get_or_create_bfi(bf_tf_from);
                        ts_.add(tpi_from, si_from, tpi_to, si_to, bfi_from,
                                sa_fp + sa_w);
                      }
                    }
                  }
                }

                // prep next iteration
                // is this the last transport of the day?
                if (std::next(dep_it) == event_times.end()) {

                  // passing midnight
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
      footpath const reflexive_fp{li_from,
                                  tt_.locations_.transfer_time_[li_from]};
      handle_fp(reflexive_fp);

      // outgoing footpaths of location
      for (auto const& fp : tt_.locations_.footpaths_out_[li_from]) {
        handle_fp(fp);
      }
    }
  }

  // finalize transfer set
  ts_.finalize();

  std::cout << "Found " << ts_.size()
            << " transfers, the size of the transfer set is "
            << ts_.size() * sizeof(transfer) << " bytes" << std::endl;
}
