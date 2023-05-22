#pragma once

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_entry {
  std::uint16_t get_stop_idx() const {
    return static_cast<uint16_t>(stop_idx_);
  }

  day_idx_t get_day_idx_start() const { return day_idx_t{day_idx_start_}; }

  day_idx_t get_day_idx_end() const { return day_idx_t{day_idx_end_}; }

  std::uint32_t stop_idx_ : STOP_IDX_BITS;
  std::uint32_t day_idx_start_ : DAY_IDX_BITS;
  std::uint32_t day_idx_end_ : DAY_IDX_BITS;
};

struct reached {
  reached() = delete;
  explicit reached(tb_preprocessing& tbp) : tbp_(tbp) {}

  void update(transport_idx_t const,
              std::uint16_t const stop_idx,
              day_idx_t const);

  std::uint16_t query(transport_idx_t const, day_idx_t const);

  tb_preprocessing& tbp_;

  // reached stops per transport
  mutable_fws_multimap<transport_idx_t, reached_entry> data_{};
};

}  // namespace nigiri::routing::tripbased
