#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

constexpr std::string first_n(bitfield const& bf, std::size_t n = 8) {
  std::string s = "";
  n = n > bf.size() ? bf.size() : n;
  for (std::size_t i = n; i != 0; --i) {
    s += std::to_string(bf[i]);
  }
  return s;
}

bitfield_idx_t tb_preprocessing::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    bitfield_idx_t bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

void tb_preprocessing::initial_transfer_computation() {

#ifndef NDEBUG
  TBDL << "Beginning initial transfer computation, look-ahead = " << lh_
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

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for (transport_idx_t tpi_from{0U};
       tpi_from < tt_.transport_traffic_days_.size(); ++tpi_from) {

    // ri from: route index from
    route_idx_t const ri_from = tt_.transport_route_[tpi_from];

    // iterate over stops of transport (skip first stop)
    auto const stops_from = tt_.route_location_seq_[ri_from];

#ifndef NDEBUG
    TBDL << "Examining " << stops_from.size() - 1 << " stops of transport "
         << tpi_from << " of route " << ri_from << "..." << std::endl;
#endif

    // si_from: stop index from
    for (std::size_t si_from = 1U; si_from != stops_from.size(); ++si_from) {

      auto const stop = timetable::stop{stops_from[si_from]};

      // li_from: location index from
      location_idx_t const li_from = stop.location_idx();

      duration_t const t_arr_from =
          tt_.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_tp_from: shift amount transport from
      int const sa_tp_from = num_midnights(t_arr_from);

#ifndef NDEBUG
      TBDL << "si_from = " << si_from << ", li_from = " << li_from
           << ", t_arr_from = " << t_arr_from << ", sa_tp_from = " << sa_tp_from
           << std::endl;
#endif
      // locations do not have footpath to themselves, insert reflexive footpath
      std::vector<footpath> footpaths_out;
      footpaths_out.reserve(tt_.locations_.footpaths_out_[li_from].size() + 1);
      footpaths_out.emplace_back(footpath{li_from, duration_t{0}});
      std::copy(tt_.locations_.footpaths_out_[li_from].begin(),
                tt_.locations_.footpaths_out_[li_from].end(),
                std::back_inserter(footpaths_out));

#ifndef NDEBUG
      TBDL << "Examining " << footpaths_out.size() << " outgoing footpaths..."
           << std::endl;
#endif

      // iterate over stops in walking range
      // fp: outgoing footpath
      for (auto const& fp : footpaths_out) {

        // if walking to the stop takes longer than the look-ahead skip the stop
        if (lh_ < fp.duration_) {
          continue;
        }

        // li_to: location index of destination of footpath
        location_idx_t const li_to = fp.target_;

        // ta: arrival time at stop_to in relation to transport_from
        duration_t const ta = t_arr_from + fp.duration_;

        // sa_fp: shift amount footpath
        int const sa_fp = num_midnights(ta) - sa_tp_from;

        // a: time of day when arriving at stop_to
        duration_t const a = time_of_day(ta);

        // a_lh: look-ahead from a
        duration_t const a_lh = a + (lh_ - fp.duration_);

#ifndef NDEBUG
        TBDL << "li_to = " << li_to << ", sa_fp = " << sa_fp << ", a = " << a
             << std::endl;
#endif

        // iterate over lines serving stop_to
        auto const routes_at_stop_to = tt_.location_routes_[li_to];

#ifndef NDEBUG
        TBDL << "Examining " << routes_at_stop_to.size()
             << " routes at li_to = " << li_to << "..." << std::endl;
#endif

        // ri_to: route index to
        for (auto const ri_to : routes_at_stop_to) {

          // ri_to might visit stop multiple times, skip if stop_to is the last
          // stop in the stop sequence of ri_to si_to: stop index to
          for (std::size_t si_to = 0U;
               si_to < tt_.route_location_seq_[ri_to].size() - 1; ++si_to) {

            auto const stop_to_cur =
                timetable::stop{tt_.route_location_seq_[ri_to][si_to]};

            // li_from: location index from
            location_idx_t const li_to_cur = stop_to_cur.location_idx();

#ifndef NDEBUG
            TBDL << "ri_to = " << ri_to << ", si_to = " << si_to
                 << ", li_to_cur = " << li_to_cur << std::endl;
#endif

            if (li_to_cur == li_to) {

              // sa_w: shift amount due to waiting for connection
              int sa_w = 0;

              // departure times of transports of route ri_to at stop si_to
              auto const event_times =
                  tt_.event_times_at_stop(ri_to, si_to, event_type::kDep);

              // all transports of route ri_to sorted by departure time at stop
              // si_to mod 1440 in ascending order
              std::vector<std::pair<duration_t, std::uint32_t>> deps_tod;
              deps_tod.reserve(event_times.size());
              for (std::uint32_t i = 0U; i != event_times.size(); ++i) {
                deps_tod.emplace_back(time_of_day(event_times[i]), i);
              }
              std::sort(deps_tod.begin(), deps_tod.end());

#ifndef NDEBUG
              TBDL << "Sorted " << deps_tod.size()
                   << " transports by departure time" << std::endl;
#endif
              // tp_to_cur_it: iterator of current element in deps_tod
              auto tp_to_cur_it =
                  std::lower_bound(deps_tod.begin(), deps_tod.end(),
                                   std::pair<duration_t, std::uint32_t>(
                                       a, 0U));  // find first departure after a

              // omega: days of transport_from that still require connection
              bitfield omega =
                  tt_.bitfields_[tt_.transport_traffic_days_[tpi_from]];

#ifndef NDEBUG
              TBDL << "Looking for earliest connecting transport after a = "
                   << a << std::endl;
              TBDL << "initial omega = " << first_n(omega) << std::endl;
#endif
              // check if any bit in omega is set to 1
              while (omega.any()) {

                // departure time of current transport in relation to time a
                duration_t dep_cur =
                    tp_to_cur_it->first + duration_t(sa_w * 1440);

                // but dep_cur before a_lh
                if (dep_cur <= a_lh) {

                  // offset from begin of tp_to interval
                  std::size_t const tp_to_offset = tp_to_cur_it->second;

                  // transport index of transport that we transfer to
                  transport_idx_t const tpi_to =
                      tt_.route_transport_ranges_[ri_to][tp_to_offset];

#ifndef NDEBUG
                  TBDL << "Examining element with index "
                       << std::distance(deps_tod.begin(), tp_to_cur_it)
                       << " of deps_tod"
                       << ", tpi_to = " << tpi_to << ", dep_cur = " << dep_cur
                       << std::endl;
#endif

#ifndef NDEBUG
                  TBDL << "different route -> " << (ri_from != ri_to)
                       << ", earlier stop -> " << (si_to < si_from)
                       << ", earlier transport -> "
                       << (tpi_to != tpi_from &&
                           (dep_cur - (tt_.event_mam(tpi_to, si_to,
                                                     event_type::kDep) -
                                       tt_.event_mam(tpi_to, si_from,
                                                     event_type::kArr)) <=
                            a - fp.duration_))
                       << std::endl;
#endif

                  // check conditions for required transfer
                  // 1. different route OR
                  // 2. earlier stop    OR
                  // 3. same route but tpi_to is earlier than tpi_from
                  bool const req =
                      ri_from != ri_to || si_to < si_from ||
                      (tpi_to != tpi_from &&
                       (dep_cur -
                            (tt_.event_mam(tpi_to, si_to, event_type::kDep) -
                             tt_.event_mam(tpi_to, si_from,
                                           event_type::kArr)) <=
                        a - fp.duration_));

                  if (req) {
                    // shift amount due to number of times transport_to passed
                    // midnight
                    int const sa_tp_to = num_midnights(
                        tt_.event_mam(tpi_to, si_to, event_type::kDep));

                    // total shift amount
                    int const sa_total = sa_tp_to - (sa_tp_from + sa_fp + sa_w);

#ifndef NDEBUG
                    TBDL << "sa_tp_to = " << sa_tp_to
                         << ", sa_total = " << sa_total << std::endl;
#endif

                    // bitfield transport to
                    bitfield const& bf_tp_to =
                        tt_.bitfields_[tt_.transport_traffic_days_[tpi_to]];

#ifndef NDEBUG
                    TBDL << "   omega = " << first_n(omega) << std::endl;
                    TBDL << "bf_tp_to = " << first_n(bf_tp_to) << std::endl;
#endif

                    // bitfield transfer from
                    bitfield bf_tf_from = omega;

                    // align bitfields and perform AND
                    if (sa_total < 0) {
                      bf_tf_from &= bf_tp_to
                                    << static_cast<unsigned>(-1 * sa_total);
#ifndef NDEBUG
                      TBDL << "shifted left by "
                           << static_cast<unsigned>(-1 * sa_total) << std::endl;
#endif
                    } else {
                      bf_tf_from &= bf_tp_to >> static_cast<unsigned>(sa_total);
#ifndef NDEBUG
                      TBDL << "shifted right by "
                           << static_cast<unsigned>(sa_total) << std::endl;
#endif
                    }

#ifndef NDEBUG
                    TBDL << "bf_tf_from = " << first_n(bf_tf_from) << std::endl;
#endif
                    // check for match
                    if (bf_tf_from.any()) {

                      // remove days that are covered by this transfer from
                      // omega
                      omega &= ~bf_tf_from;

#ifndef NDEBUG
                      TBDL << "new omega =  " << first_n(omega) << std::endl;
#endif

                      // construct and add transfer to transfer set
                      bitfield_idx_t const bfi_from =
                          get_or_create_bfi(bf_tf_from);
                      // bitfield transfer to
                      bitfield bf_tf_to = bf_tf_from;
                      if (sa_total < 0) {
                        bf_tf_to >>= static_cast<unsigned>(-1 * sa_total);
                      } else {
                        bf_tf_to <<= static_cast<unsigned>(sa_total);
                      }
                      bitfield_idx_t const bfi_to = get_or_create_bfi(bf_tf_to);
                      transfer const t{tpi_to, li_to, bfi_from, bfi_to};
                      ts_.add(tpi_from, li_from, t);

#ifndef NDEBUG
                      TBDL << "transfer added:" << std::endl;
                      TBDL << "tpi_from = " << tpi_from
                           << ", li_from = " << li_from << std::endl;
                      TBDL << "tpi_to = " << tpi_to << ", li_to = " << li_to
                           << std::endl;
                      TBDL << "bf_tf_from = " << first_n(bf_tf_from)
                           << std::endl;
                      TBDL << "  bf_tf_to = " << first_n(bf_tf_to) << std::endl;
#endif
                    }
#ifndef NDEBUG
                    else {
                      TBDL << "no bitfield match" << std::endl;
                    }
#endif
                  }

#ifndef NDEBUG
                  else {
                    TBDL << "not required" << std::endl;
                  }
#endif
                } else {
                  // examined departure time is greater than look-ahead
                  break;
                }

                // prep next iteration
                // is this the last transport of the day?
                if (std::next(tp_to_cur_it) == deps_tod.end()) {
                  // passing midnight
                  ++sa_w;
                  // start with the earliest transport on the next day
                  tp_to_cur_it = deps_tod.begin();
                } else {
                  ++tp_to_cur_it;
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