#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "geo/box.h"
#include "geo/latlng.h"

#include "nigiri/routing/clasz_mask.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/one_to_n_state.h"
#include "nigiri/routing/transfer_time_settings.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

// One-to-N: for each origin cell and departure time (sample), the Pareto set
// of (arrival time, number of trips) at every other cell. Cells reach stops by
// walking. Each search keeps a single label row (no journey reconstruction);
// the samples of an origin are combined by a pluggable accumulator.
//
// Walk-only labels (k = 0) come only from the direct cell-to-cell walks. Unlike
// routing::search (fastest_direct), a walk through a stop
// (cell -> stop -> cell) neither is a label nor prunes rides.
namespace nigiri::routing {

// Walks that connect the cells to the timetable. Built once per set of cells,
// shared read-only by all searches.
struct one_to_n_targets {
  std::uint32_t n_cells() const {
    return static_cast<std::uint32_t>(access_.size());
  }

  // Per cell: walks from the cell to stops (start of a journey).
  vecvec<cell_idx_t, stop_walk> access_;

  // Per location: walks from the stop to cells (end of a journey).
  vecvec<location_idx_t, cell_walk> egress_;

  // The egress walks grouped by cell.
  vecvec<cell_idx_t, stop_walk> egress_by_cell_;

  // Per cell: walk-only connections to other cells (labels with k = 0).
  vecvec<cell_idx_t, cell_walk> direct_;
};

// access[c]: walks from cell c to stops.
// egress[c]: walks from stops to cell c. Empty: walking is symmetric, the
//            access walks are used.
// direct[c]: walks from cell c to other cells. Empty: no walk-only labels.
// Locations are expanded like location_match_mode::kIntermodal; duplicates
// keep the shortest walk.
one_to_n_targets make_one_to_n_targets(
    timetable const&,
    std::vector<std::vector<stop_walk>> const& access,
    std::vector<std::vector<stop_walk>> const& egress = {},
    std::vector<std::vector<cell_walk>> const& direct = {});

// Points of a regular grid covering the box, spaced by about `spacing` meters,
// row by row from south-west to north-east.
std::vector<geo::latlng> make_grid(geo::box const&, double spacing);

// Beeline walks as a stand-in for street routing: distance / speed, rounded up
// to minutes. Only locations served by a route are used as stops.
struct beeline_walks {
  double speed_{1.2};  // m/s
  duration_t max_walk_{15};  // cell <-> stop
  duration_t max_direct_{0};  // cell <-> cell, 0 = no walk-only labels
};

one_to_n_targets make_beeline_targets(timetable const&,
                                      std::span<geo::latlng const> cells,
                                      beeline_walks const&);

struct one_to_n_label {
  // Trips: 0 = walk only, 1 = no transfer, 2 = one transfer, ...
  std::uint8_t k_;

  // From the sample's departure time to the arrival at the cell.
  duration_t travel_time_;

  friend bool operator==(one_to_n_label const&,
                         one_to_n_label const&) = default;
};

// Labels of one search (origin, departure time), grouped by cell.
struct one_to_n_sample {
  // Pareto set of cell c: ascending k, strictly decreasing travel time.
  std::span<one_to_n_label const> operator[](cell_idx_t const c) const {
    auto const i = to_idx(c);
    return std::span{labels_}.subspan(offsets_[i],
                                      offsets_[i + 1U] - offsets_[i]);
  }

  // Cells with at least one label, ascending. Never contains the origin.
  std::span<cell_idx_t const> reached() const { return reached_; }

  std::vector<std::uint32_t> offsets_;  // n_cells + 1
  std::vector<one_to_n_label> labels_;
  std::vector<cell_idx_t> reached_;
};

// Combines the samples of an origin. clone() creates one instance per worker
// thread. A worker processes one origin at a time:
//   begin_origin(o)
//   add_sample(departure, labels)  for each departure time, ascending
//   end_origin(o)
// Instances of different workers run concurrently; output they share needs
// synchronization.
struct one_to_n_accumulator {
  one_to_n_accumulator() = default;
  one_to_n_accumulator(one_to_n_accumulator const&) = default;
  one_to_n_accumulator(one_to_n_accumulator&&) = default;
  one_to_n_accumulator& operator=(one_to_n_accumulator const&) = default;
  one_to_n_accumulator& operator=(one_to_n_accumulator&&) = default;
  virtual ~one_to_n_accumulator() = default;

  virtual std::unique_ptr<one_to_n_accumulator> clone() const = 0;
  virtual void begin_origin(cell_idx_t origin) = 0;
  virtual void add_sample(unixtime_t departure, one_to_n_sample const&) = 0;
  virtual void end_origin(cell_idx_t origin) = 0;
};

struct one_to_n_query {
  // Departure times (samples), searched in ascending order for each origin.
  std::vector<unixtime_t> departures_;

  // Empty: all cells.
  std::vector<cell_idx_t> origins_;

  duration_t max_travel_time_{90};
  std::uint8_t max_transfers_{kMaxTransfers};
  profile_idx_t prf_idx_{0U};
  clasz_mask_t allowed_claszes_{all_clasz_allowed()};
  bool require_bike_transport_{false};
  bool require_car_transport_{false};
  bool no_compulsory_reservation_{false};
  transfer_time_settings transfer_time_settings_{};

  // Lower bounds from each origin: cells that cannot be reached within the
  // travel time limit don't hold back the target pruning bound.
  bool prune_unreachable_cells_{true};

  unsigned n_threads_{1U};
};

struct one_to_n_stats {
  std::uint64_t n_origins_{0U};
  std::uint64_t n_searches_{0U};
  // Searches skipped: no cell reachable within the travel time limit.
  std::uint64_t n_searches_skipped_{0U};
  std::uint64_t n_labels_{0U};
  // Rounds in which the target pruning bound (worst cell label) was applied.
  std::uint64_t n_bound_rounds_{0U};
  // Summed over the worker threads.
  std::chrono::nanoseconds origin_lb_time_{0};
  std::chrono::nanoseconds search_time_{0};
  std::chrono::nanoseconds accumulator_time_{0};
  // Wall time, including the lower bounds to the cells.
  std::chrono::nanoseconds total_time_{0};
};

// Forward searches only. Requires rtt == nullptr or an rt_timetable for tt.
one_to_n_stats one_to_n(timetable const&,
                        rt_timetable const*,
                        one_to_n_targets const&,
                        one_to_n_query const&,
                        one_to_n_accumulator const&);

}  // namespace nigiri::routing
