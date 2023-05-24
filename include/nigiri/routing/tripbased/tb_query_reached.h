#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_entry {
  bool dominates(reached_entry const& o) const {
    return transport_segment_idx_ <= o.transport_segment_idx_ &&
           stop_idx_ <= o.stop_idx_ && n_transfers_ <= o.n_transfers_;
  }
  std::uint32_t transport_segment_idx_;
  std::uint16_t stop_idx_;
  std::uint16_t n_transfers_;
};

struct reached {
  reached() = delete;
  explicit reached(tb_preprocessing& tbp, day_idx_t const query_day)
      : tbp_(tbp), query_day_(query_day) {
    data_.resize(tbp_.tt_.n_routes());
  }

  void update(day_idx_t const transport_day,
              transport_idx_t const transport_idx,
              std::uint16_t const stop_idx,
              std::uint16_t const n_transfers);

  std::uint16_t query(transport_idx_t const,
                      day_idx_t const,
                      std::uint32_t const n_transfers);

  tb_preprocessing& tbp_;
  day_idx_t const query_day_;

  // reached stops per route
  std::vector<pareto_set<reached_entry>> data_;
  // mutable_fws_multimap<transport_idx_t, reached_entry> data_{};
};

}  // namespace nigiri::routing::tripbased
