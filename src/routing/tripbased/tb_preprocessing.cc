
#include "nigiri/types.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

namespace nigiri::routing::tripbased {

void tb_preprocessing::initial_transfer_computation(timetable& tt) {
  // init bitfields hashmap with bitfields that are already used by the timetable
  for(bitfield_idx_t bfi{0U}; bfi < tt.bitfields_.size(); ++bfi) { // bfi: bitfield index
    bitfield_to_bitfield_idx_.emplace(tt.bitfields_[bfi], bfi);
  }

  // iterate over all trips of the timetable
  for(transport_idx_t tpi_from{0U}; tpi_from < tt.transport_traffic_days_.size(); ++tpi_from) { // tpi: transport idx from
    route_idx_t ri_from = tt.transport_route_[tpi_from]; // ri from: route index from

    // iterate over stops of transport (skip first stop)
    auto stops_from = tt.route_location_seq_[ri_from];
    for(std::size_t si_from = 1U; si_from < stops_from.size(); ++si_from) { // si_from: stop index from
      location_idx_t li_from{stops_from[si_from]}; // li_from: location index from

      duration_t t_arr_from = tt.event_mam(tpi_from, si_from, event_type::kArr);
      int satp_from = num_midnights(t_arr_from); // sa_from: shift amount transport from

      // iterate over stops in walking range
      auto footpaths_out = it_range{tt.locations_.footpaths_out_[li_from]};
      for(auto fp : footpaths_out) {
        location_idx_t li_to = fp.target_; // li_to: location index of destination of footpath

        duration_t ta = t_arr_from + fp.duration_; // ta: arrival time at stop_to in relation to transport_from
        int safp = num_midnights(ta) - satp_from; // safp: shift amount footpath
        duration_t a = time_of_day(ta); // a: time of day when arriving at stop_to

        // iterate over lines serving stop_to
        auto routes_at_stop_to = it_range{tt.location_routes_[li_to]};
        for(auto ri_to : routes_at_stop_to) { // ri_to: route index to
          // ri_to might visit stop multiple times, skip if stop_to is the last stop in the stop sequence of the ri_to
          for(std::size_t si_to = 0U; si_to < tt.route_location_seq_[ri_to].size() - 1; ++si_to) { // si_to: stop index to
            if(li_to == tt.route_location_seq_[ri_to][si_to]) {
              int sach = safp; // sach: shift amount due to changing transports

              // ...

            }
          }

        }
      }
    }
  }
}

}; // namespace nigiri::routing::tripbased