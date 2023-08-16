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
    return transport_segment_idx_ <= o.transport_segment_idx_ &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_ &&
           time_walk_ <= o.time_walk_;
  }
  transport_segment_idx_t transport_segment_idx_;
  std::uint16_t stop_idx_;
  std::uint16_t n_transfers_;
  std::uint16_t time_walk_;
};
#else
struct reached_entry {
  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_ <= o.transport_segment_idx_ &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_;
  }
  transport_segment_idx_t transport_segment_idx_;
  std::uint16_t stop_idx_;
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
              std::uint16_t conststop_idx,
              std::uint16_t const n_transfers,
              std::uint16_t const time_walk);

  std::uint16_t stop(transport_segment_idx_t const,
                     std::uint16_t const n_transfers,
                     std::uint16_t const time_walk);

  std::uint16_t walk(transport_segment_idx_t const,
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
