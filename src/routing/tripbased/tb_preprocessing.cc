
#include "nigiri/types.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

namespace nigiri::routing::tripbased {

void tb_preprocessing::initial_transfer_computation(timetable& tt) {
  // init bitfields hashmap with bitfields that are already used by the timetable
  for(bitfield_idx_t bfi{0U}; bfi < tt.bitfields_.size(); ++bfi) { // bfi: bitfield_index
    bitfield_to_bitfield_idx_.emplace(tt.bitfields_[bfi], bfi);
  }

  // iterate over all trips of the timetable
  for(transport_idx_t tpi_from{0U}; tpi_from < tt.transport_traffic_days_.size(); ++tpi_from) { // tpi: transport_idx_from
    route_idx_t ri_from = tt.transport_route_[tpi_from]; // ri_from: route_index_from

    // iterate over stops of transport (skip first stop)
    auto stops_from = tt.route_location_seq_[ri_from];
    for(std::size_t si_from = 1U; si_from < stops_from.size(); ++si_from) { // si_from: stop_index_from
      int satp_from = num_midnights(tt.event_mam(tpi_from, si_from, event_type::kArr)); // sa_from: shift_amount_transport_from

      // iterate over stops in walking range
    }
  }
}

}; // namespace nigiri::routing::tripbased