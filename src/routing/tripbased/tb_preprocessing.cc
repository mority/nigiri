#include "utl/get_or_create.h"

#include "nigiri/types.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

bitfield_idx_t tb_preprocessing::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    bitfield_idx_t bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf,bfi);
    return bfi;});
}

void tb_preprocessing::initial_transfer_computation() {
  // init bitfields hashmap with bitfields that are already used by the timetable
  for(bitfield_idx_t bfi{0U}; bfi < tt_.bitfields_.size(); ++bfi) { // bfi: bitfield index
    bitfield_to_bitfield_idx_.emplace(tt_.bitfields_[bfi], bfi);
  }

#ifndef NDEBUG
  TBDL << "Added " << bitfield_to_bitfield_idx_.size() << " KV-pairs to bitfield_to_bitfield_idx_" << std::endl;
#endif

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for(transport_idx_t tpi_from{0U}; tpi_from < tt_.transport_traffic_days_.size(); ++tpi_from) {

    // ri from: route index from
    route_idx_t const ri_from = tt_.transport_route_[tpi_from];



    // iterate over stops of transport (skip first stop)
    auto const stops_from = tt_.route_location_seq_[ri_from];

#ifndef NDEBUG
    TBDL << "Examining " << stops_from.size()-1  << " stops of transport " << tpi_from << " of route " << ri_from << "..." <<  std::endl;
#endif

    // si_from: stop index from
    for(std::size_t si_from = 1U; si_from != stops_from.size(); ++si_from) {

      auto const stop = timetable::stop{stops_from[si_from]};

      // li_from: location index from
      location_idx_t const li_from = stop.location_idx();

      duration_t const t_arr_from =
          tt_.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_from: shift amount transport from
      int const satp_from = num_midnights(t_arr_from);

#ifndef NDEBUG
      TBDL << "si_from = " << si_from << ", li_from = " << li_from << ", t_arr_from = " << t_arr_from << ", satp_from = " << satp_from <<  std::endl;
#endif
      // locations do not have footpath to themselves, insert reflexive footpath
      std::vector<footpath> footpaths_out;
      footpaths_out.reserve(tt_.locations_.footpaths_out_[li_from].size() + 1);
      footpaths_out.emplace_back(footpath{li_from, duration_t {0}});
      std::copy(tt_.locations_.footpaths_out_[li_from].begin(), tt_.locations_.footpaths_out_[li_from].end(), std::back_inserter(footpaths_out));

#ifndef NDEBUG
      TBDL << "Examining " << footpaths_out.size() << " outgoing footpaths..." <<  std::endl;
#endif

      // iterate over stops in walking range
      // fp: outgoing footpath
      for(auto const& fp : footpaths_out) {

        // li_to: location index of destination of footpath
        location_idx_t const li_to = fp.target_;

        // ta: arrival time at stop_to in relation to transport_from
        duration_t const ta = t_arr_from + fp.duration_;

        // safp: shift amount footpath
        int const safp = num_midnights(ta) - satp_from;

        // a: time of day when arriving at stop_to
        duration_t const a = time_of_day(ta);

#ifndef NDEBUG
        TBDL << "li_to = " << li_to << ", safp = " << safp << ", a = " << a << std::endl;
#endif

        // iterate over lines serving stop_to
        auto const routes_at_stop_to = tt_.location_routes_[li_to];

#ifndef NDEBUG
        TBDL << "Examining " << routes_at_stop_to.size() << " routes at li_to = " << li_to << "..." << std::endl;
#endif

        // ri_to: route index to
        for(auto const ri_to : routes_at_stop_to) {

          // ri_to might visit stop multiple times, skip if stop_to is the last stop in the stop sequence of ri_to
          // si_to: stop index to
          for(std::size_t si_to = 0U; si_to < tt_.route_location_seq_[ri_to].size() - 1; ++si_to) {

            auto const stop_to_cur = timetable::stop{tt_.route_location_seq_[ri_to][si_to]};

            // li_from: location index from
            location_idx_t const li_to_cur = stop_to_cur.location_idx();

#ifndef NDEBUG
            TBDL << "ri_to = " << ri_to << ", si_to = " << si_to << ", li_to_cur = " << li_to_cur << std::endl;
#endif

            if(li_to_cur == li_to) {

              // sach: shift amount due to changing transports
              int sach = safp;

              // departure times of transports of route ri_to at stop si_to
              auto const event_times =
                  tt_.event_times_at_stop(ri_to, si_to, event_type::kDep);

              // all transports of route ri_to sorted by departure time at stop si_to mod 1440 in ascending order
              std::vector<std::pair<duration_t,std::size_t>> deps_tod(event_times.size());
              for(std::size_t i = 0U; i != event_times.size(); ++i) {
                deps_tod.emplace_back(time_of_day(event_times[i]), i);
              }
              std::sort(deps_tod.begin(), deps_tod.end());

#ifndef NDEBUG
              TBDL << "Sorted " << deps_tod.size() << " transports by departure time" << std::endl;
#endif
              // first transport in deps_tod that departs after time of day a
              std::size_t tp_to_init = deps_tod.size();
              for(std::size_t i = 0U; i < deps_tod.size(); ++i) {
                if(a <= deps_tod[i].first) {
                  tp_to_init = i;
                  break;
                }
              }

              // initialize index of currently examined transport in deps_tod
              std::size_t tp_to_cur = tp_to_init;

              // true if midnight was passed while waiting for connecting trip
              bool midnight = false;

              // omega: days of transport_from that still require connection
              bitfield omega =
                  tt_.bitfields_[tt_.transport_traffic_days_[tpi_from]];

#ifndef NDEBUG
              TBDL << "Looking for earliest connecting transport after a = " << a << std::endl;
#endif
              // check if any bit in omega is set to 1
              while(omega.any()) {
                // check if tp_to_cur is valid, if not continue at beginning of day
                if(tp_to_cur == deps_tod.size() && !midnight) {
                  midnight = true;
                  ++sach;
                  tp_to_cur = 0U;
#ifndef NDEBUG
                  TBDL << "Passed midnight" << std::endl;
#endif
                }

                // time of day of departure of current transport
                duration_t dep_cur = deps_tod[tp_to_cur].first;
                if(midnight) {
                  dep_cur += duration_t{1440U};
                }

                // offset from begin of tp_to interval
                std::size_t const tp_to_offset = deps_tod[tp_to_cur].second;

                // transport index of transport that we transfer to
                transport_idx_t const tpi_to =
                    tt_.route_transport_ranges_[ri_to][static_cast<std::uint32_t>(tp_to_offset)];

#ifndef NDEBUG
                TBDL << "Examining tp_to_cur = " << tp_to_cur << ", tpi_to = " << tpi_to << ", dep_cur = " << dep_cur << std::endl;
#endif

#ifndef NDEBUG
                TBDL << "different route -> " << (ri_from != ri_to) << ", earlier stop -> " << (si_to < si_from) << ", earlier transport -> " << (tpi_to != tpi_from
                                                                                                                                                                                && (dep_cur - (tt_.event_mam(tpi_to,si_to,event_type::kDep) -
                                                                                                                                                                                               tt_.event_mam(tpi_to, si_from, event_type::kArr)) <= a - fp.duration_)) << std::endl;
#endif
                // check conditions for required transfer
                // 1. different route OR
                // 2. earlier stop    OR
                // 3. same route but tpi_to is earlier than tpi_from
                bool const req = ri_from != ri_to
                           || si_to < si_from
                           || (tpi_to != tpi_from
                               && (dep_cur - (tt_.event_mam(tpi_to,si_to,event_type::kDep) -
                           tt_.event_mam(tpi_to, si_from, event_type::kArr)) <= a - fp.duration_));

                if(req) {
                  // shift amount due to number of times transport_to passed midnight
                  int const satp_to = num_midnights(
                      tt_.event_mam(tpi_to,si_to,event_type::kDep));

                  // total shift amount
                  int const sa_total = satp_to - (satp_from + sach);

                  // align bitfields and perform AND
                  // bitfield transfer from
                  bitfield bf_tf_from = omega;
                  if (sa_total < 0) {
                    bf_tf_from &=
                        tt_.bitfields_[tt_.transport_traffic_days_[tpi_to]] << static_cast<std::size_t>(-1 * sa_total);
                  } else {
                    bf_tf_from &=
                        tt_.bitfields_[tt_.transport_traffic_days_[tpi_to]] >> static_cast<std::size_t>(sa_total);
                  }

                  // check for match
                  if(bf_tf_from.any()) {

                    // remove days that are covered by this transfer from omega
                    omega &= ~bf_tf_from;

                    // construct and add transfer to transfer set
                    bitfield_idx_t const bfi_from = get_or_create_bfi(bf_tf_from);
                    // bitfield transfer to
                    bitfield bf_tf_to = bf_tf_from;
                    if(sa_total < 0) {
                      bf_tf_to <<= static_cast<std::size_t>(-1 * sa_total);
                    } else {
                      bf_tf_to >>= static_cast<std::size_t>(sa_total);
                    }
                    bitfield_idx_t const bfi_to = get_or_create_bfi(bf_tf_to);
                    transfer const t{tpi_to, li_to, bfi_from, bfi_to};
                    ts_.add(tpi_from, li_from, t);
#ifndef NDEBUG
                      TBDL << "transfer added" << std::endl;
#endif
                  }
#ifndef NDEBUG
                  else {
                    TBDL << "required but no bitfield match" << std::endl;
                  }
#endif
                }

#ifndef NDEBUG
                else {
                  TBDL << "not required" << std::endl;
                }
#endif

                // prep next iteration
                ++tp_to_cur;
                // check if we examined all transports
                if(tp_to_cur == tp_to_init) {
                  break;
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