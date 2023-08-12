#include "nigiri/routing/tripbased/query/reached.h"

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
  data_[tt_.transport_route_[transport_idx(transport_segment_idx)].v_].add(
      reached_entry{transport_segment_idx, stop_idx, n_transfers});
}

std::uint16_t reached::query(
    transport_segment_idx_t const transport_segment_idx,
    std::uint16_t const n_transfers) {
  auto const route_idx =
      tt_.transport_route_[transport_idx(transport_segment_idx)];

  auto stop_idx_min =
      static_cast<uint16_t>(tt_.route_location_seq_[route_idx].size() - 1);
  // find minimal stop index among relevant entries
  for (auto const& re : data_[route_idx.v_]) {
    // only entries with less or equal n_transfers and less or equal
    // transport_segment_idx are relevant
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.stop_idx_ < stop_idx_min) {
      stop_idx_min = re.stop_idx_;
    }
  }

  return stop_idx_min;
}