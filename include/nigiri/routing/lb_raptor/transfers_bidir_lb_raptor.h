#pragma once

#include <array>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/search.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

// Transfers-only variant of `bidir_lb_raptor`.
//
// `bidir_lb_raptor` keeps a full round table of travel-time lower bounds
// (`round_times[k][l]`). Those labels do two jobs: they prune the search, and -
// because they are additive - they act as parent pointers, so a pattern can be
// reconstructed by inverting the arithmetic (`find_in_prev_round`).
//
// Here the metric is the *number of transports* instead. Since "reachable with
// <= k transports" is monotone in k, the whole round table collapses into a
// single `std::uint8_t` per location and direction: `lb_[l]` is the first round
// in which `l` was reached. That makes the search a plain first-touch BFS -
// every location is relaxed at most once, ever.
//
// The price is reconstruction: with a pure transfer count the predecessor of
// `l` is no longer unique. *Any* location `p` with `lb_[p] == lb_[l] - 1` that
// can reach `l` with one transport (plus at most one footpath) is a valid
// predecessor, so reconstruction becomes a bounded enumeration over a
// predecessor DAG rather than a single chain walk.
//
// To keep that enumeration from picking absurd predecessors, a *single* flat
// travel-time array per direction is still maintained (`fwd_time_`/`bwd_time_`,
// one value per location instead of one per location and round). It is filled
// at first touch only - so it is an estimate, not the minimum - and is used
// purely to rank meetpoints and to order predecessor candidates. Set
// `order_preds_by_time_` to false to measure the algorithm without it.
struct transfers_bidir_lb_raptor_stats {
  void reset() {
    pattern_reconstructions_ = 0UL;
    pattern_repetitions_ = 0UL;
    truncated_patterns_ = 0UL;
    unrealizable_patterns_ = 0UL;
    pruned_meetpoints_ = 0UL;
    chain_enumerations_ = 0UL;
    pred_expansions_ = 0UL;
    dead_ends_ = 0UL;
  }

  std::uint64_t pattern_reconstructions_;
  std::uint64_t pattern_repetitions_;
  // meetpoints whose forward or backward chain enumeration came up empty
  std::uint64_t truncated_patterns_;
  // no journey could be built for a complete pattern
  std::uint64_t unrealizable_patterns_;
  // dropped by `max_patterns_per_round_` before being reconstructed
  std::uint64_t pruned_meetpoints_;
  // complete chains produced by the DAG enumeration (both directions)
  std::uint64_t chain_enumerations_;
  // calls to `find_preds`
  std::uint64_t pred_expansions_;
  // `find_preds` returned nothing although no terminal was reached
  std::uint64_t dead_ends_;
};

struct transfers_bidir_lb_raptor {
  static constexpr auto kUnreachableLb =
      std::numeric_limits<std::uint8_t>::max();
  static constexpr auto kUnreachableTime =
      std::numeric_limits<std::uint16_t>::max();

  void execute(timetable const&,
               rt_timetable const*,
               query const&,
               bool arrive_by = false);

private:
  void reset(unsigned n_locations, unsigned n_lb_routes);

  template <direction SearchDir>
  void init(timetable const&, query const&, bool arrive_by);

  template <direction SearchDir>
  bool run(timetable const&, query const&, unsigned k);

  void meetpoints_to_patterns(timetable const&,
                              rt_timetable const*,
                              query const&,
                              bool arrive_by);

  // All `p` with `lb[p] == lb[cur] - 1` that reach `cur` with one transport,
  // optionally followed by one footpath. Ordered best-first and truncated to
  // `max_preds_per_step_`.
  template <direction SearchDir>
  void find_preds(timetable const&,
                  query const&,
                  location_idx_t cur,
                  std::vector<location_idx_t>& out);

  // Depth-first enumeration of the predecessor DAG from `m` down to a terminal.
  // Fills `fwd_chains_` / `bwd_chains_` with the predecessor sequences (not
  // including `m`), at most `max_chains_per_meetpoint_` of them.
  template <direction SearchDir>
  void enumerate_chains(timetable const&, query const&, location_idx_t m);

public:
  // location -> minimum number of transports from the start (fwd) resp. to the
  // destination (bwd). This replaces the whole `round_times` table.
  vector_map<location_idx_t, std::uint8_t> fwd_lb_;
  vector_map<location_idx_t, std::uint8_t> bwd_lb_;
  // Travel-time estimate at first touch. Ranking/ordering only, never a bound.
  vector_map<location_idx_t, std::uint16_t> fwd_time_;
  vector_map<location_idx_t, std::uint16_t> bwd_time_;

  bitvec fwd_station_mark_;
  bitvec bwd_station_mark_;
  bitvec prev_station_mark_;
  bitvec is_start_;
  bitvec is_dest_;
  bitvec lb_route_mark_;

  // Lower bound on what a journey through a meetpoint can achieve. With
  // transport counts both halves are exact in the relaxed model: the forward
  // search needs `fwd_lb_[l]` transports to reach `l`, the backward search
  // `bwd_lb_[l]` to get from `l` to the destination.
  struct scored_meetpoint {
    friend bool operator<(scored_meetpoint const& a,
                          scored_meetpoint const& b) {
      return std::tie(a.transfers_, a.travel_time_lb_) <
             std::tie(b.transfers_, b.travel_time_lb_);
    }

    location_idx_t l_;
    std::uint8_t transfers_;
    std::uint32_t travel_time_lb_;
  };

  // Same meaning as in `bidir_lb_raptor`: meetpoints per round that are turned
  // into patterns, best score first.
  unsigned max_patterns_per_round_{50U};
  // Predecessor candidates followed per step of the DAG walk. 1 reproduces the
  // single-chain behaviour of `bidir_lb_raptor::reconstruct`.
  unsigned max_preds_per_step_{3U};
  // Complete chains produced per meetpoint and direction.
  unsigned max_chains_per_meetpoint_{4U};
  // Patterns emitted per meetpoint (the fwd x bwd cross product is capped).
  unsigned max_patterns_per_meetpoint_{8U};
  // Order predecessor candidates and meetpoints by the travel-time estimate.
  bool order_preds_by_time_{true};

  // One enumerated predecessor sequence. A pattern is bounded by
  // `kMaxTransfers + 2`, so a chain is too - a fixed array keeps the
  // enumeration free of nested allocations.
  struct chain {
    std::array<location_idx_t, kMaxTransfers + 2U> l_{};
    std::uint8_t n_{0U};
  };

  std::map<location_idx_t, std::uint16_t> min_;
  std::vector<location_idx_t> meetpoints_;
  std::vector<scored_meetpoint> scored_meetpoints_;
  std::vector<location_idx_t> current_pattern_;
  std::vector<location_idx_t> chain_stack_;
  std::vector<chain> fwd_chains_;
  std::vector<chain> bwd_chains_;
  // One candidate buffer per recursion depth.
  std::array<std::vector<location_idx_t>, kMaxTransfers + 2U> pred_scratch_;
  std::set<std::array<location_idx_t, kMaxTransfers + 2U>> patterns_;
  // All alternatives, one per distinct pattern - deliberately *not* a
  // pareto_set, see `bidir_lb_raptor`.
  std::vector<journey> journeys_;
  transfers_bidir_lb_raptor_stats stats_;
};

// Entry point matching `bidir_lb_raptor_search`, so both can be benchmarked
// through the same interface.
routing_result transfers_bidir_lb_raptor_search(
    timetable const&,
    rt_timetable const*,
    search_state&,
    query,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing
