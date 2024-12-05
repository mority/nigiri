#pragma once

#include "nigiri/types.h"

namespace nigiri::routing {

struct n_to_all_label {
  duration_t travel_time_;
  std::uint16_t n_transfers_;
  location_idx_t source_;
};

}  // namespace nigiri::routing