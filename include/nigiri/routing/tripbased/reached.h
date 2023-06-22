#pragma once

#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/routing/tripbased/transport_segment.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_entry {
  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_ <= o.transport_segment_idx_ &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_;
  }
  transport_segment_idx_t transport_segment_idx_;
  std::uint16_t stop_idx_;
  std::uint16_t n_transfers_;
};

struct reached {
  reached() = delete;
  explicit reached(timetable const& tt) : tt_(tt) {
    data_.resize(tt.n_routes());
  }

  void reset();

  void update(transport_segment_idx_t const,
              std::uint16_t const stop_idx,
              std::uint16_t const n_transfers);

  std::uint16_t query(transport_segment_idx_t const,
                      std::uint16_t const n_transfers);

  timetable const& tt_;

  // reached stops per route
  std::vector<pareto_set<reached_entry>> data_;
  // mutable_fws_multimap<transport_idx_t, reached_entry> data_{};
};

}  // namespace nigiri::routing::tripbased