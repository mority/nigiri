#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_entry {
  std::uint16_t get_stop_idx() const {
    return static_cast<uint16_t>(stop_idx_);
  }

  day_idx_t get_day_idx_start() const { return day_idx_t{day_idx_}; }

  bool dominates(reached_entry const& re) const {
    return stop_idx_ <= re.stop_idx_ && day_idx_ <= re.day_idx_ &&
           num_transfers_ <= re.num_transfers_;
  }

  std::uint32_t stop_idx_ : STOP_IDX_BITS;
  std::uint32_t day_idx_ : DAY_IDX_BITS;
  std::uint32_t num_transfers_ : NUM_TRANSFERS_BITS;
};

struct reached {
  reached() = delete;
  explicit reached(tb_preprocessing& tbp, unsigned n_transports) : tbp_(tbp) {
    data_.resize(n_transports);
  }

  void update(transport_idx_t const,
              std::uint32_t const stop_idx,
              day_idx_t const,
              std::uint32_t const num_transfers);

  std::uint16_t query(transport_idx_t const,
                      day_idx_t const,
                      std::uint32_t const num_transfers);

  tb_preprocessing& tbp_;

  // reached stops per transport
  std::vector<pareto_set<reached_entry>> data_;
  // mutable_fws_multimap<transport_idx_t, reached_entry> data_{};
};

}  // namespace nigiri::routing::tripbased
