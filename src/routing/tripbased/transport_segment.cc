#include "nigiri/routing/tripbased/transport_segment.h"
#include "nigiri/timetable.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void transport_segment::print(std::ostream& out, timetable const& tt) const {
  std::string transferred_from_str = transferred_from_ == TRANSFERRED_FROM_NULL
                                         ? "INIT"
                                         : std::to_string(transferred_from_);
  out << tt.transport_name(get_transport_idx()) << " from "
      << tt.locations_.names_
             .at(stop{tt.route_location_seq_
                          [tt.transport_route_[get_transport_idx()]]
                          [stop_idx_start_]}
                     .location_idx())
             .view()
      << " to "
      << tt.locations_.names_
             .at(stop{
                 tt.route_location_seq_
                     [tt.transport_route_[get_transport_idx()]][stop_idx_end_]}
                     .location_idx())
             .view()
      << ", transferred_from: " << transferred_from_str << "\n";
}
