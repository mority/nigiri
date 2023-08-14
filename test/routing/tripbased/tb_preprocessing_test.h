#pragma once

#include "nigiri/routing/tripbased/settings.h"

namespace nigiri::routing::tripbased::test {

bool transfers_equal(transfer const& a, transfer const& b) {
  return a.bitfield_idx_ == b.bitfield_idx_ &&
         a.transport_idx_to_ == b.transport_idx_to_ &&
         a.stop_idx_to_ == b.stop_idx_to_ &&
         a.passes_midnight_ == b.passes_midnight_;
}

}  // namespace nigiri::routing::tripbased::test