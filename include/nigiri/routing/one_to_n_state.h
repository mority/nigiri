#pragma once

#include <cstdint>
#include <vector>

#include "nigiri/common/delta_t.h"
#include "nigiri/types.h"

namespace nigiri::routing {

// A target of a one-to-N search, e.g. a point of a regular grid. Cells are
// origins and destinations; they reach stops by walking.
using cell_idx_t = cista::strong<std::uint32_t, struct _cell_idx>;

// Walk to a cell.
struct cell_walk {
  cell_idx_t cell_;
  duration_t duration_;
};

// Walk between a cell and a stop.
struct stop_walk {
  location_idx_t location_;
  duration_t duration_;
};

// Label found by the search: cell reached with k trips at time_.
struct one_to_n_emitted {
  cell_idx_t cell_;
  std::uint8_t k_;
  delta_t time_;
};

// Target state of one search (raptor<..., search_mode::kOneToN>).
// Owned by the caller and reused across searches, one per worker thread.
struct one_to_n_state {
  // Per location: egress walks to the cells.
  vecvec<location_idx_t, cell_walk> const* egress_{nullptr};

  // Per cell: best arrival so far (delta to the search base). Only improves.
  std::vector<delta_t> best_;

  // Cells improved in the current round.
  bitvec improved_;

  // Cells that count for the target pruning bound: all cells that may still
  // be reached within the travel time limit, except the origin.
  bitvec relevant_;
  std::uint32_t n_relevant_{0U};
  std::uint32_t n_reached_{0U};  // relevant cells with a label

  // Labels in the order found, i.e. ascending k for each cell.
  std::vector<one_to_n_emitted> emitted_;

  // Rounds in which the target pruning bound was tightened.
  std::uint32_t n_bound_rounds_{0U};
};

}  // namespace nigiri::routing
