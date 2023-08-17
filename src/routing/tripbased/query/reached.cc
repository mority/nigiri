#include "nigiri/routing/tripbased/query/reached.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void reached::reset() {
  for (auto& ps : data_) {
    ps.clear();
  }
}

#ifdef TB_MIN_WALK

void reached::update(transport_segment_idx_t const transport_segment_idx,
                     std::uint16_t const stop_idx,
                     std::uint16_t const n_transfers,
                     std::uint16_t const time_walk) {
  data_[tt_.transport_route_[transport_idx(transport_segment_idx)].v_].add(
      reached_entry{transport_segment_idx, stop_idx, n_transfers, time_walk});
}

std::uint16_t reached::stop(transport_segment_idx_t const transport_segment_idx,
                            std::uint16_t const n_transfers,
                            std::uint16_t const time_walk) {
  auto const route_idx =
      tt_.transport_route_[transport_idx(transport_segment_idx)];
  auto stop_idx_min =
      static_cast<uint16_t>(tt_.route_location_seq_[route_idx].size() - 1);
  for (auto const& re : data_[route_idx.v_]) {
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.time_walk_ <= time_walk && re.stop_idx_ < stop_idx_min) {
      stop_idx_min = re.stop_idx_;
    }
  }
  return stop_idx_min;
}

std::uint16_t reached::walk(transport_segment_idx_t const transport_segment_idx,
                            std::uint16_t const n_transfers,
                            std::uint16_t const stop_idx) {
  auto const route_idx =
      tt_.transport_route_[transport_idx(transport_segment_idx)];
  auto time_walk_min = std::numeric_limits<std::uint16_t>::max();
  for (auto const& re : data_[route_idx.v_]) {
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.stop_idx_ <= stop_idx && re.time_walk_ < time_walk_min) {
      time_walk_min = re.time_walk_;
    }
  }
  return time_walk_min;
}

#elifdef TB_TRANSFER_CLASS

void reached::update(transport_segment_idx_t const transport_segment_idx,
                     std::uint16_t const stop_idx,
                     std::uint16_t const n_transfers,
                     std::uint8_t const transfer_class_max,
                     std::uint8_t const transfer_class_sum) {
  data_[tt_.transport_route_[transport_idx(transport_segment_idx)].v_].add(
      reached_entry{transport_segment_idx, stop_idx, n_transfers,
                    transfer_class_max, transfer_class_sum});
}

std::uint16_t reached::stop(transport_segment_idx_t const transport_segment_idx,
                            std::uint16_t const n_transfers,
                            std::uint8_t const transfer_class_max,
                            std::uint8_t const transfer_class_sum) {
  auto const route_idx =
      tt_.transport_route_[transport_idx(transport_segment_idx)];
  auto stop_idx_min =
      static_cast<uint16_t>(tt_.route_location_seq_[route_idx].size() - 1);
  for (auto const& re : data_[route_idx.v_]) {
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.transfer_class_max_ <= transfer_class_max &&
        re.transfer_class_sum_ <= transfer_class_sum &&
        re.stop_idx_ < stop_idx_min) {
      stop_idx_min = re.stop_idx_;
    }
  }
  return stop_idx_min;
}

std::pair<std::uint8_t, std::uint8_t> reached::transfer_class(
    transport_segment_idx_t const transport_segment_idx,
    std::uint16_t const n_transfers,
    std::uint16_t const stop_idx) {
  auto const route_idx =
      tt_.transport_route_[transport_idx(transport_segment_idx)];
  auto transfer_class_max_min = std::numeric_limits<std::uint8_t>::max();
  auto transfer_class_sum_min = std::numeric_limits<std::uint8_t>::max();
  for (auto const& re : data_[route_idx.v_]) {
    if (re.n_transfers_ <= n_transfers &&
        re.transport_segment_idx_ <= transport_segment_idx &&
        re.stop_idx_ <= stop_idx) {
      if (re.transfer_class_max_ < transfer_class_max_min) {
        transfer_class_max_min = re.transfer_class_max_;
      }
      if (re.transfer_class_sum_ < transfer_class_sum_min) {
        transfer_class_sum_min = re.transfer_class_sum_;
      }
    }
  }
  return std::pair{transfer_class_max_min, transfer_class_sum_min};
}

#else

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

#endif