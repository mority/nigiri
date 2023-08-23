#pragma once

#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/tripbased/query/transport_segment.h"
#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

#ifdef TB_MIN_WALK
struct reached_entry {
  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_.value() <= o.transport_segment_idx_.value() &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_ &&
           time_walk_ <= o.time_walk_;
  }
  transport_segment_idx_t transport_segment_idx_;
  stop_idx_t stop_idx_;
  std::uint16_t n_transfers_;
  std::uint16_t time_walk_;
};
#elifdef TB_TRANSFER_CLASS
struct reached_entry {
  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_.value() <= o.transport_segment_idx_.value() &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_ &&
           transfer_class_max_ <= o.transfer_class_max_ &&
           transfer_class_sum_ <= o.transfer_class_sum_;
  }
  transport_segment_idx_t transport_segment_idx_;
  stop_idx_t stop_idx_;
  std::uint16_t n_transfers_;
  std::uint8_t transfer_class_max_;
  std::uint8_t transfer_class_sum_;
};
#else
struct reached_entry {
  //  reached_entry(transport_segment_idx_t transport_segment_idx,
  //                stop_idx_t stop_idx,
  //                std::uint16_t n_transfers)
  //      : transport_segment_idx_(transport_segment_idx),
  //        stop_idx_(stop_idx),
  //        n_transfers_(n_transfers) {}

  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_.value() <= o.transport_segment_idx_.value() &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_;
  }
  transport_segment_idx_t transport_segment_idx_;
  stop_idx_t stop_idx_;
  std::uint16_t n_transfers_;
};
#endif

struct reached {
  reached() = delete;
  explicit reached(timetable const& tt) : tt_(tt) {
    data_.resize(tt.n_routes());
  }

  void reset();

#ifdef TB_MIN_WALK
  void update(transport_segment_idx_t const,
              stop_idx_t const stop_idx,
              std::uint16_t const n_transfers,
              std::uint16_t const time_walk);

  std::uint16_t stop(transport_segment_idx_t const,
                     std::uint16_t const n_transfers,
                     std::uint16_t const time_walk);

  std::uint16_t walk(transport_segment_idx_t const,
                     std::uint16_t const n_transfers,
                     stop_idx_t const stop_idx);
#elifdef TB_TRANSFER_CLASS
  void update(transport_segment_idx_t const,
              stop_idx_t const stop_idx,
              std::uint16_t const n_transfers,
              std::uint8_t const transfer_class_max,
              std::uint8_t const transfer_class_sum);

  std::uint16_t stop(transport_segment_idx_t const,
                     std::uint16_t const n_transfers,
                     std::uint8_t const transfer_class_max,
                     std::uint8_t const transfer_class_sum);

  std::pair<std::uint8_t, std::uint8_t> transfer_class(
      transport_segment_idx_t const,
      std::uint16_t const n_transfers,
      std::uint16_t const stop_idx);
#else
  void update(transport_segment_idx_t const,
              std::uint16_t const stop_idx,
              std::uint16_t const n_transfers);

  std::uint16_t query(transport_segment_idx_t const,
                      std::uint16_t const n_transfers);
#endif

  timetable const& tt_;

  // reached stops per route
  std::vector<pareto_set<reached_entry>> data_;
};

}  // namespace nigiri::routing::tripbased
