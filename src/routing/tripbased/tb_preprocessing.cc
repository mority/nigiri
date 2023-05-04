#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

constexpr std::string first_n(bitfield const& bf, std::size_t n = 16) {
  std::string s;
  n = n > bf.size() ? bf.size() : n;
  for (std::size_t i = n; i + 1 != 0; --i) {
    s += bf[i] ? "1" : "0";
  }
  return s;
}

bitfield_idx_t tb_preprocessing::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

bool tb_preprocessing::earliest_times::update(location_idx_t li_new,
                                              duration_t time_new,
                                              bitfield const& bf) {

  bitfield bf_new = bf;

  // find first tuple of this li_new
  auto et_cur = std::lower_bound(
      data_.begin(), data_.end(), li_new,
      [](earliest_time const& et, location_idx_t const& l) constexpr {
        return et.location_idx_ < l;
      });

  // no entry for this li_new
  if (et_cur == data_.end()) {
    data_.emplace_back(li_new, time_new, tbp_.get_or_create_bfi(bf_new));
    return true;
  } else if (li_new < et_cur->location_idx_) {
    data_.emplace(et_cur, li_new, time_new, tbp_.get_or_create_bfi(bf_new));
    return true;
  }

  // iterator to first entry that would be erased, overwrite with new entry
  // instead
  auto overwrite_spot = data_.end();

  // iterate entries for this li_new
  while (et_cur != data_.end() && et_cur->location_idx_ == li_new) {
    if (bf_new.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return false;
    } else if (time_new < et_cur->time_) {
      // new time is better than current time, update bit set of current
      // time
      bitfield bf_cur = tbp_.tt_.bitfields_[et_cur->bf_idx_] & ~bf_new;
      if (bf_cur.none()) {
        // if current time is no longer needed, we remove it from the vector
        if (overwrite_spot == data_.end()) {
          overwrite_spot = et_cur;
          ++et_cur;
        } else {
          data_.erase(et_cur);
        }
      } else {
        // save updated bitset index of current time
        et_cur->bf_idx_ = tbp_.get_or_create_bfi(bf_cur);
        ++et_cur;
      }
    } else if (time_new == et_cur->time_) {
      // entry for this time already exists
      if ((bf_new & ~tbp_.tt_.bitfields_[et_cur->bf_idx_]).none()) {
        // all bits of bf_new already covered
        return false;
      } else {
        bf_new |= tbp_.tt_.bitfields_[et_cur->bf_idx_];
        if (overwrite_spot == data_.end()) {
          overwrite_spot = et_cur;
          ++et_cur;
        } else {
          data_.erase(et_cur);
        }
      }
    } else {
      // new time is worse than current time, update bit set of new time
      bf_new &= ~tbp_.tt_.bitfields_[et_cur->bf_idx_];
      ++et_cur;
    }
  }

  if (bf_new.any()) {
    if (overwrite_spot == data_.end()) {
      data_.emplace(et_cur, li_new, time_new, tbp_.get_or_create_bfi(bf_new));
    } else {
      overwrite_spot->time_ = time_new;
      overwrite_spot->bf_idx_ = tbp_.get_or_create_bfi(bf_new);
    }
    return true;
  } else {
    return false;
  }
}

void tb_preprocessing::build_transfer_set(bool uturn_removal, bool reduction) {

#ifndef NDEBUG
  TBDL << "Beginning initial transfer computation, sa_w_max_ = " << sa_w_max_
       << std::endl;
#endif

  // init bitfields hashmap with bitfields that are already used by the
  // timetable
  for (bitfield_idx_t bfi{0U}; bfi < tt_.bitfields_.size();
       ++bfi) {  // bfi: bitfield index
    bitfield_to_bitfield_idx_.emplace(tt_.bitfields_[bfi], bfi);
  }

#ifndef NDEBUG
  TBDL << "Added " << bitfield_to_bitfield_idx_.size()
       << " KV-pairs to bitfield_to_bitfield_idx_" << std::endl;
#endif

  // earliest arrival time per stop
  earliest_times ets_arr{*this};

  // earliest time a connecting trip may depart per stop
  earliest_times ets_ch{*this};

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for (transport_idx_t tpi_from{0U};
       tpi_from != tt_.transport_traffic_days_.size(); ++tpi_from) {

    if (reduction) {
      // clear earliest times
      ets_arr.clear();
      ets_ch.clear();
    }

    // ri from: route index from
    auto const ri_from = tt_.transport_route_[tpi_from];

    // iterate over stops of transport (skip first stop)
    auto const stops_from = tt_.route_location_seq_[ri_from];

#ifndef NDEBUG
    TBDL << "-----------------------------------------------------------------"
         << std::endl;
#endif

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

      auto const stop = timetable::stop{stops_from[si_from]};

      // li_from: location index from
      auto const li_from = stop.location_idx();

      auto const t_arr_from =
          tt_.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_tp_from: shift amount transport from
      auto const sa_tp_from = num_midnights(t_arr_from);

      auto const& bf_tp_from =
          tt_.bitfields_[tt_.transport_traffic_days_[tpi_from]];

#ifndef NDEBUG
      TBDL << "tpi_from = " << tpi_from << ", ri_from = " << ri_from
           << ", si_from = " << si_from << ", li_from = " << li_from << "("
           << std::basic_string<char>{tt_.locations_.names_[li_from].begin(),
                                      tt_.locations_.names_[li_from].end()}
           << "), t_arr_from = " << t_arr_from
           << ", sa_tp_from = " << sa_tp_from << std::endl;
#endif

      if (reduction) {
        // init earliest times
        ets_arr.update(li_from, t_arr_from, bf_tp_from);
        for (auto const& fp : tt_.locations_.footpaths_out_[li_from]) {
          ets_arr.update(fp.target_, t_arr_from + fp.duration_, bf_tp_from);
          ets_ch.update(fp.target_, t_arr_from + fp.duration_, bf_tp_from);
        }
      }

      // outgoing footpaths of location
      for (auto const& fp : tt_.locations_.footpaths_out_[li_from]) {

        // li_to: location index of destination of footpath
        auto const li_to = fp.target_;

        // ta: arrival time at stop_to in relation to transport_from
        auto const ta = t_arr_from + fp.duration_;

        // sa_fp: shift amount footpath
        auto const sa_fp = num_midnights(ta) - sa_tp_from;

        // a: time of day when arriving at stop_to
        auto const a = time_of_day(ta);

#ifndef NDEBUG
        TBDL << "li_to = " << li_to << "("
             << std::basic_string<char>{tt_.locations_.names_[li_from].begin(),
                                        tt_.locations_.names_[li_from].end()}
             << "), sa_fp = " << sa_fp << ", a = " << a << std::endl;
#endif

        // iterate over lines serving stop_to
        auto const routes_at_stop_to = tt_.location_routes_[li_to];

#ifndef NDEBUG
        TBDL << "Examining " << routes_at_stop_to.size() << " routes at "
             << std::basic_string<char>{tt_.locations_.names_[li_from].begin(),
                                        tt_.locations_.names_[li_from].end()}
             << "..." << std::endl;
#endif

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
              int sa_w = 0;

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

#ifndef NDEBUG
              TBDL << "tpi_from = " << tpi_from << ", si_from = " << si_from
                   << ", from location: "
                   << std::basic_string<
                          char>{tt_.locations_.names_[li_from].begin(),
                                tt_.locations_.names_[li_from].end()}
                   << ", ri_to = " << ri_to << ", si_to = " << si_to
                   << ", to location:  = "
                   << std::basic_string<
                          char>{tt_.locations_.names_[li_to_cur].begin(),
                                tt_.locations_.names_[li_to_cur].end()}
                   << std::endl
                   << "Looking for earliest connecting transport after a = "
                   << a << ", initial omega = " << first_n(omega) << std::endl;
#endif
              // check if any bit in omega is set to 1
              while (omega.any()) {

                // departure time of current transport in relation to time a
                auto const dep_cur = *dep_it + duration_t{sa_w * 1440};

                // offset from begin of tp_to interval
                auto const tp_to_offset =
                    std::distance(event_times.begin(), dep_it);

                // transport index of transport that we transfer to
                auto const tpi_to =
                    tt_.route_transport_ranges_[ri_to][static_cast<std::size_t>(
                        tp_to_offset)];

#ifndef NDEBUG
                TBDL << "tpi_to = " << tpi_to << " departing at " << dep_cur
                     << ": different route -> " << (ri_from != ri_to)
                     << ", earlier stop -> " << (si_to < si_from)
                     << ", earlier transport -> "
                     << (tpi_to != tpi_from &&
                         (dep_cur -
                              (tt_.event_mam(tpi_to, si_to, event_type::kDep) -
                               tt_.event_mam(tpi_to, si_from,
                                             event_type::kArr)) <=
                          a - fp.duration_))
                     << std::endl;
#endif

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
                  auto const sa_total = sa_tp_to - (sa_tp_from + sa_fp + sa_w);

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

#ifndef NDEBUG
                    TBDL << "new omega =  " << first_n(omega) << std::endl;
#endif
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
                                               &ets_arr, &bf_tf_from,
                                               &ets_ch]() {
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
                          auto const ets_arr_res = ets_arr.update(
                              li_to_later, t_arr_to_later, bf_tf_from);
                          impr = impr || ets_arr_res;
                          for (auto const& fp_later :
                               tt_.locations_.footpaths_out_[li_to_later]) {
                            auto const eta =
                                t_arr_to_later + fp_later.duration_;
                            auto const ets_arr_fp_res = ets_arr.update(
                                fp_later.target_, eta, bf_tf_from);
                            auto const ets_ch_fp_res = ets_ch.update(
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
                                -sa_total);

#ifndef NDEBUG
                        TBDL << "transfer added:" << std::endl
                             << "tpi_from = " << tpi_from
                             << ", li_from = " << li_from << std::endl
                             << "tpi_to = " << tpi_to << ", li_to = " << li_to
                             << std::endl
                             << "bf_tf_from = " << first_n(bf_tf_from)
                             << std::endl
                             << "  shift_amount = " << -sa_total << std::endl;

#endif
                      }
                    }
                  }
                }

                // prep next iteration
                // is this the last transport of the day?
                if (std::next(dep_it) == event_times.end()) {

#ifndef NDEBUG
                  if (omega.any()) {
                    TBDL << "passing midnight" << std::endl;
                  }
#endif
                  // passing midnight
                  ++sa_w;

                  if (sa_w_max_ < sa_w) {
#ifndef NDEBUG
                    TBDL << "Maximum waiting time reached" << std::endl;
#endif
                    break;
                  }

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
    }
  }

  // finalize transfer set
  ts_.finalize();
}
