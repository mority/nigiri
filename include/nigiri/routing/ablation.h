#pragma once

#include <cstdint>
#include <string_view>

#include "nigiri/types.h"

namespace nigiri::routing {

// Leave-one-out switches for benchmarks: every arm runs the same binary. They
// are read once from the environment variable NIGIRI_ABLATION, a list of
// tokens separated by "+". Empty or unset: the full algorithm. The active
// switches are reported per response as search stat "ablation" (code()).
//
// Each switch removes one component. All components only prune or reuse, so
// every arm has to return the same journeys.
//
//   no-sharing    Pong: the backward searches of one iteration do not share
//                 labels; reset_arrivals() runs before every backward search.
//                 The forward-label bounds are copied before, so they stay.
//   no-pingbound  Pong: backward searches are not bounded by the labels of
//                 the forward search but use Dijkstra lower bounds, as before
//                 nigiri #384.
//   no-skip       Pong: known journeys are searched backward again (#454).
//   no-limit      Pong: known journeys do not limit the arrival of the
//                 forward searches (#456).
//   no-lb         Pong and RQ: zero lower bounds instead of the Dijkstra on
//                 the lower-bound graph. Plain target pruning remains.
//   no-routelb    Pong and RQ: no per-route lower-bound check before a route
//                 is scanned (75777089).
//   no-lazyreset  Pong and RQ: reset_arrivals() resets all rounds of the round
//                 table instead of initializing each round on first use.
struct ablation {
  std::uint64_t code() const {
    return (no_sharing_ ? 1U : 0U) | (no_ping_bound_ ? 2U : 0U) |
           (no_skip_ ? 4U : 0U) | (no_limit_ ? 8U : 0U) | (no_lb_ ? 16U : 0U) |
           (no_route_lb_ ? 32U : 0U) | (no_lazy_reset_ ? 64U : 0U);
  }

  bool no_sharing_{false};
  bool no_ping_bound_{false};
  bool no_skip_{false};
  bool no_limit_{false};
  bool no_lb_{false};
  bool no_route_lb_{false};
  bool no_lazy_reset_{false};
};

// Aborts on an unknown token: motis falls back to RAPTOR on Pong exceptions,
// which would turn a typo into a silently wrong benchmark.
ablation parse_ablation(std::string_view);

ablation const& get_ablation();

// Pong stops at this distance from the query time (#403: 6 days). Read once
// from NIGIRI_PONG_LOOKAHEAD_DAYS (1 to 14), default 6. Reported per response
// as search stat "pong_lookahead_days".
duration_t get_pong_max_lookahead();

}  // namespace nigiri::routing
