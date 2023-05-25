#include "nigiri/routing/tripbased/tb_query_reached.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void reached::reset() {
  for (auto& ps : data_) {
    ps.clear();
  }
}

void reached::update(transport_segment_idx_t const transport_segment_idx,
                     std::uint16_t const stop_idx,
                     std::uint16_t const n_transfers) {
  data_[tbp_.tt_.transport_route_[transport_idx(transport_segment_idx)]].add(
      reached_entry{transport_segment_idx, stop_idx, n_transfers});
}

std::uint16_t reached::query(
    transport_segment_idx_t const transport_segment_idx,
    std::uint16_t const n_transfers) {
  auto const route_idx =
      tbp_.tt_.transport_route_[transport_idx(transport_segment_idx)];

  auto stop_idx = std::numeric_limits<std::uint16_t>::max();
  // find minimal stop index among relevant entries
  for (auto const& re : data_[route_idx]) {
    // only entries with less or equal n_transfers and less or equal
    // transport_segment_idx are relevant
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.stop_idx_ < stop_idx) {
      stop_idx = re.stop_idx_;
    }
  }
  // no relevant entry found
  if (stop_idx == std::numeric_limits<std::uint16_t>::max()) {
    // return stop index of final stop of the route
    stop_idx = static_cast<uint16_t>(
        tbp_.tt_.route_location_seq_[route_idx].size() - 1);
  }

  return stop_idx;
}