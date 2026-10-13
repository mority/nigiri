#pragma once

#include <chrono>
#include <optional>

#include "nigiri/routing/query.h"
#include "nigiri/routing/search.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

// Reversed range query (Halm 2019, "Algorithms for the Pagination Problem on
// Public Transit Networks", Section 4.2.1).
//
// A forward pre-trip query with an open later end asks for the first
// min_connection_count_ journeys departing at or after the start of the
// interval. rRAPTOR scans the departure events at the source from latest to
// earliest, so it needs an upper bound before its first run, which the
// interval estimator has to guess.
//
// This driver scans the arrival events at the destination from earliest to
// latest instead, one backward RAPTOR run per arrival event on one shared
// round table. After scanning arrival t the profile is complete for every
// arrival up to t, so the scan stops as soon as min_connection_count_
// journeys are found; no upper bound is needed. The only quantity needed up
// front is the earliest arrival, which one forward earliest-arrival search
// from the start of the interval computes. The number of runs is driven by
// the arrival events at the destination, not by the departure events at the
// source. Journeys departing before the interval are discarded through
// query::dest_time_bound_.
//
// Symmetric for a backward query with an open earlier end (arrive-by).
//
// Requires a start interval, min_connection_count_ > 0 and exactly the open
// end in search direction; throws otherwise. Journeys are reported like
// pong_search: in the orientation of the query, sorted by start time.
// The reported interval starts at the query interval and ends one minute
// after the latest start time of the reported journeys.
template <typename AlgoState>
routing_result reversed_search(
    timetable const&,
    rt_timetable const*,
    search_state&,
    AlgoState&,
    query,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing
