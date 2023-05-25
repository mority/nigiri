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
  transport_segment_idx_t const transport_segment_idx_;
  std::uint16_t const stop_idx_;
  std::uint16_t const n_transfers_;
};

struct reached {
  reached() = delete;
  explicit reached(tb_preprocessing& tbp) : tbp_(tbp) {
    data_.resize(tbp_.tt_.n_routes());
  }

  void reset();

  void update(transport_segment_idx_t const,
              std::uint16_t const stop_idx,
              std::uint16_t const n_transfers);

  std::uint16_t query(transport_segment_idx_t const,
                      std::uint16_t const n_transfers);

  tb_preprocessing& tbp_;

  // reached stops per route
  std::vector<pareto_set<reached_entry>> data_;
  // mutable_fws_multimap<transport_idx_t, reached_entry> data_{};
};

}  // namespace nigiri::routing::tripbased
