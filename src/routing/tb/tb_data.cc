#include "nigiri/routing/tb/tb_data.h"

#include "nigiri/common/day_list.h"
#include "nigiri/routing/tb/segment_info.h"
#include "nigiri/timetable.h"

namespace nigiri::routing::tb {

segment_idx_t tb_data::to_segment(timetable const& tt,
                                  transfer const& t) const {
  return transport_first_segment_
             [tt.route_transport_ranges_[t.get_route()].from_ +
              static_cast<std::uint32_t>(t.transport_offset_)] +
         static_cast<std::uint32_t>(t.to_segment_offset_);
}

void tb_data::print(std::ostream& out, timetable const& tt) const {
  for (auto t = transport_idx_t{0U}; t != tt.next_transport_idx(); ++t) {
    out << "transport " << tt.transport_name(t) << " @ "
        << tt.days(tt.bitfields_[tt.transport_traffic_days_[t]]) << ":\n";
    for (auto const s : get_segment_range(t)) {
      out << "  " << segment_info{tt, *this, s}
          << ", #transfers=" << segment_transfers_[s].size() << "\n";
      for (auto const transfer : segment_transfers_[s]) {
        out << "    -> " << segment_info{tt, *this, to_segment(tt, transfer)}
            << "\n";
      }
    }
  }
}

}  // namespace nigiri::routing::tb