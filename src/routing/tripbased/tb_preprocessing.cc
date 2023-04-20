#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

constexpr std::string first_n(bitfield const& bf, std::size_t n = 16) {
  std::string s;
  n = n > bf.size() ? bf.size() : n;
  for (std::size_t i = n; i + 1 != 0; --i) {
    s += std::to_string(bf[i]);
  }
  return s;
}

std::string print_transport(timetable const& tt, transport_idx_t const& tpi) {
  std::string s;
  for (auto const att_comb_idx : tt.transport_section_attributes_[tpi]) {
    for (auto const att_idx : tt.attribute_combinations_[att_comb_idx]) {
      s += tt.attributes_[att_idx].code_;
      s += ": ";
      s += tt.attributes_[att_idx].text_;
      s += " | ";
    }
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

void tb_preprocessing::initial_transfer_computation() {

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

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for (transport_idx_t tpi_from{0U};
       tpi_from != tt_.transport_traffic_days_.size(); ++tpi_from) {

    // ri from: route index from
    auto const ri_from = tt_.transport_route_[tpi_from];

    // iterate over stops of transport (skip first stop)
    auto const stops_from = tt_.route_location_seq_[ri_from];

#ifndef NDEBUG
    TBDL << "-----------------------------------------------------------------"
         << std::endl;
#endif

    // si_from: stop index from
    for (std::size_t si_from = 1U; si_from != stops_from.size(); ++si_from) {

      auto const stop = timetable::stop{stops_from[si_from]};

      // li_from: location index from
      auto const li_from = stop.location_idx();

      auto const t_arr_from =
          tt_.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_tp_from: shift amount transport from
      auto const sa_tp_from = num_midnights(t_arr_from);

#ifndef NDEBUG
      TBDL << "tpi_from = " << tpi_from << ", ri_from = " << ri_from
           << ", si_from = " << si_from << ", li_from = " << li_from << "("
           << std::basic_string<char>{tt_.locations_.names_[li_from].begin(),
                                      tt_.locations_.names_[li_from].end()}
           << "), t_arr_from = " << t_arr_from
           << ", sa_tp_from = " << sa_tp_from << std::endl;
#endif

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
          for (std::size_t si_to = 0U;
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
              // tp_to_cur_it: iterator of current element in deps_tod
              auto tp_to_cur_it =
                  std::lower_bound(event_times.begin(), event_times.end(), a,
                                   [&](auto&& x, auto&& y) {
                                     return time_of_day(x) < time_of_day(y);
                                   });

              // no departure on this day at or after a
              if (tp_to_cur_it == event_times.end()) {
                ++sa_w;  // start looking on the following day
                tp_to_cur_it =
                    event_times.begin();  // with the earliest transport
              }

              // omega: days of transport_from that still require connection
              bitfield omega =
                  tt_.bitfields_[tt_.transport_traffic_days_[tpi_from]];

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
                auto const dep_cur = *tp_to_cur_it + duration_t{sa_w * 1440};

                // offset from begin of tp_to interval
                auto const tp_to_offset =
                    std::distance(event_times.begin(), tp_to_cur_it);

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
                  auto const sa_tp_to = num_midnights(
                      tt_.event_mam(tpi_to, si_to, event_type::kDep));

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

                    // construct and add transfer to transfer set
                    auto const bfi_from = get_or_create_bfi(bf_tf_from);
                    // bitfield transfer to
                    auto bf_tf_to = bf_tf_from;
                    if (sa_total < 0) {
                      bf_tf_to <<= static_cast<unsigned>(-1 * sa_total);
                    } else {
                      bf_tf_to >>= static_cast<unsigned>(sa_total);
                    }
                    auto const bfi_to = get_or_create_bfi(bf_tf_to);
                    ts_.add(tpi_from, li_from, tpi_to, li_to, bfi_from, bfi_to);

#ifndef NDEBUG
                    TBDL << "transfer added:" << std::endl
                         << "tpi_from = " << tpi_from
                         << ", li_from = " << li_from << std::endl
                         << "tpi_to = " << tpi_to << ", li_to = " << li_to
                         << std::endl
                         << "bf_tf_from = " << first_n(bf_tf_from) << std::endl
                         << "  bf_tf_to = " << first_n(bf_tf_to) << std::endl;
#endif
                  }
                }

                // prep next iteration
                // is this the last transport of the day?
                if (std::next(tp_to_cur_it) == event_times.end()) {

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
                  tp_to_cur_it = event_times.begin();
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