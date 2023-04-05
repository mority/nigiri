#include "../../../include/nigiri/routing/tripbased/tb_preprocessing.h"

namespace nigiri::routing::tripbased {

void tb_preprocessing::initial_transfer_computation(timetable& tt) {
  // init bitfields hashmap with bitfields that are already used by the timetable
  for(bitfield_idx_t i{0U}; i < tt.bitfields_.size(); ++i) {
    bitfield_to_bitfield_idx_.emplace(tt.bitfields_[i], i);
  }

  // iterate over all trips of the timetable
  for(transport_idx_t transport_idx_from{0U}; transport_idx_from < tt.transport_traffic_days_.size(); ++transport_idx_from) {
    // iterate over stops of transport (skip first stop)
    route_idx_t route_idx_from = tt.transport_route_[transport_idx_from];
    for(auto stop : tt.route_location_seq_[route_idx_from]) {
      location_idx_t location_idx_from(stop);
      int sigma_t = tt.event_mam(transport_idx_from, route_idx_from,)
    }
  }
}

}; // namespace nigiri::routing::tripbased