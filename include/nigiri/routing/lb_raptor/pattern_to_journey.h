#pragma once

#include <optional>
#include <vector>

#include "nigiri/common/interval.h"
#include "nigiri/routing/journey.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;

// Turns a transfer pattern (as found by the time-invariant bidir_lb_raptor)
// into a concrete journey.
//
// The pattern is a sequence of transfer stations in *travel* order. For
// kForward, pattern.front() is matched by `q.start_` / `q.start_match_mode_`
// and pattern.back() by `q.destination_` / `q.dest_match_mode_`; for kBackward
// (arrive_by, i.e. a flipped query - see `query::flip_dir`) it is the other way
// round. `q.start_time_` always refers to the location matched by `q.start_`,
// so for kBackward it is the arrival time at the journey's destination.
//
// Since the pattern only says *that* the stations are connected by some route
// (not at which times), the actual transports are determined greedily: hop by
// hop, `get_alternative` picks the earliest (kForward) / latest
// (kBackward) transport connecting two consecutive pattern stations. The
// resulting journey is therefore not necessarily Pareto-optimal.
//
// Returns `std::nullopt` if the pattern cannot be realized (no transport for
// one of the hops, no matching start/destination offset, or the journey
// exceeds `q.max_travel_time_`).

template <direction SearchDir>
std::optional<journey> pattern_to_journey(timetable const&,
                                          rt_timetable const*,
                                          query const&,
                                          std::vector<location_idx_t> const&);

// Realizes `pattern` for one explicit anchor time: the earliest (kForward) /
// latest (kBackward) journey that sets off at or after (before) `anchor`. The
// result is tightened, i.e. `journey::start_time_` is the departure of the
// access leg that just catches the first transport - so anchoring the next call
// one minute past it yields the next distinct departure.
template <direction SearchDir>
std::optional<journey> pattern_to_journey_at(timetable const&,
                                             rt_timetable const*,
                                             query const&,
                                             std::vector<location_idx_t> const&,
                                             unixtime_t anchor);

}  // namespace nigiri::routing
