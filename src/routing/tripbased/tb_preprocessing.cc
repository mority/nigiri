
#include "nigiri/types.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

namespace nigiri::routing::tripbased {

void tb_preprocessing::initial_transfer_computation(timetable& tt) {
  // init bitfields hashmap with bitfields that are already used by the timetable
  for(bitfield_idx_t bfi{0U}; bfi < tt.bitfields_.size(); ++bfi) { // bfi: bitfield index
    bitfield_to_bitfield_idx_.emplace(tt.bitfields_[bfi], bfi);
  }

  // iterate over all trips of the timetable
  // tpi: transport idx from
  for(transport_idx_t tpi_from{0U}; tpi_from < tt.transport_traffic_days_.size(); ++tpi_from) {

    // ri from: route index from
    route_idx_t ri_from = tt.transport_route_[tpi_from];

    // iterate over stops of transport (skip first stop)
    auto stops_from = tt.route_location_seq_[ri_from];
    // si_from: stop index from
    for(std::size_t si_from = 1U; si_from < stops_from.size(); ++si_from) {

      // li_from: location index from
      location_idx_t li_from{stops_from[si_from]};

      duration_t t_arr_from = tt.event_mam(tpi_from, si_from, event_type::kArr);

      // sa_from: shift amount transport from
      int satp_from = num_midnights(t_arr_from);

      // iterate over stops in walking range
      auto footpaths_out = it_range{tt.locations_.footpaths_out_[li_from]};
      // fp: outgoing footpath
      for(auto fp : footpaths_out) {

        // li_to: location index of destination of footpath
        location_idx_t li_to = fp.target_;

        // ta: arrival time at stop_to in relation to transport_from
        duration_t ta = t_arr_from + fp.duration_;

        // safp: shift amount footpath
        int safp = num_midnights(ta) - satp_from;

        // a: time of day when arriving at stop_to
        duration_t a = time_of_day(ta);

        // iterate over lines serving stop_to
        auto routes_at_stop_to = it_range{tt.location_routes_[li_to]};
        // ri_to: route index to
        for(auto ri_to : routes_at_stop_to) {
          // ri_to might visit stop multiple times, skip if stop_to is the last stop in the stop sequence of the ri_to
          // si_to: stop index to
          for(std::size_t si_to = 0U; si_to < tt.route_location_seq_[ri_to].size() - 1; ++si_to) {
            if(li_to == tt.route_location_seq_[ri_to][si_to]) {

              // sach: shift amount due to changing transports
              int sach = safp;

              auto const tp_to_interval = tt.route_transport_ranges_[ri_to];

              // all transports of route ri_to sorted by departure time mod 1440 in ascending order
              auto const deps_tod = tt.event_times_at_stop(ri_to, si_to, event_type::kDep);

              // first transport in deps_tod that departs after time of day a
              std::size_t tp_to_init = deps_tod.size();
              for(std::size_t i = 0U; i < deps_tod.size(); ++i) {
                if(a <= deps_tod[i]) {
                  tp_to_init = i;
                  break;
                }
              }

              std::size_t tp_to = tp_to_init;

              // true if midnight was passed while waiting for connecting trip
              bool midnight = false;

              // omega: days of transport_from that still require connection
              bitfield omega = tt.bitfields_[tt.transport_traffic_days_[tpi_from]];

              // check if any bit in omega is set to 1
              while(omega.any()) {
                // check if tp_to is valid, if not continue at beginning of day
                if(tp_to == deps_tod.size() && !midnight) {
                  midnight = true;
                  sach++;
                  tp_to = 0U;
                }

              }
            }
          }

        }
      }
    }
  }
}

}; // namespace nigiri::routing::tripbased