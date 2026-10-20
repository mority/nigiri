#include "nigiri/routing/tb/tb_data.h"

#include <algorithm>
#include <bit>

#include "nigiri/common/day_list.h"
#include "nigiri/routing/tb/segment_info.h"
#include "nigiri/timetable.h"

namespace nigiri::routing::tb {

transfer_layout get_transfer_layout(timetable const& tt) {
  auto max_transports = std::uint32_t{0U};
  auto max_stops = std::uint32_t{0U};
  for (auto r = route_idx_t{0U}; r != tt.n_routes(); ++r) {
    max_transports =
        std::max(max_transports, tt.route_transport_ranges_[r].size());
    max_stops = std::max(max_stops, tt.route_location_seq_[r].size());
  }

  // bits for the indices 0, ..., n - 1
  auto const bits = [](std::uint32_t const n) {
    return static_cast<std::uint8_t>(std::bit_width(n == 0U ? 0U : n - 1U));
  };
  auto const l = transfer_layout{.route_bits_ = bits(tt.n_routes()),
                                 .transport_bits_ = bits(max_transports),
                                 .stop_bits_ = bits(max_stops)};
  utl::verify(l.route_bits_ + l.transport_bits_ + l.stop_bits_ +
                      transfer_layout::kDayOffsetBits <=
                  64U,
              "tb: a transfer can't address {} routes with up to {} "
              "transports and {} stops",
              tt.n_routes(), max_transports, max_stops);
  return l;
}

void tb_data::print(std::ostream& out, timetable const& tt) const {
  for (auto t = transport_idx_t{0U}; t != tt.next_transport_idx(); ++t) {
    out << "transport " << tt.transport_name(t) << " @ "
        << tt.days(tt.bitfields_[tt.transport_traffic_days_[t]]) << ":\n";
    for (auto const s : get_segment_range(t)) {
      out << "  " << segment_info{tt, *this, s}
          << ", #transfers=" << segment_transfers_[s].size() << "\n";
      for (auto const transfer : segment_transfers_[s]) {
        out << "    -> " << segment_info{tt, *this, transfer.to_segment_}
            << "\n";
      }
    }
  }
}

}  // namespace nigiri::routing::tb