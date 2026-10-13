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
//
// Order of the reported journeys (Halm 2019, Section 3.1.2 and 4.2):
//
//   kArrival    the n earliest arriving journeys after the query time
//               (Halm's rr_arr). What the scan produces by itself.
//   kDeparture  the n earliest departing journeys after the query time, the
//               same question Pong and the forward range query answer
//               (Halm's rr_dep). The scan runs ahead by a look-ahead pool of
//               lf * n journeys; the merge takes the earliest departing known
//               journey and, for every transfer count whose frontier (the
//               latest departure up to which all journeys with that count
//               are known) lies before it, fills the gap with one forward
//               search from the frontier plus one backward search per
//               journey found, i.e. one Pong iteration. Within one transfer
//               count the profile has the same order by departure and by
//               arrival, so a fill advances the frontier to the departure it
//               found. The scan's own completeness is used as in Halm: a
//               journey arriving inside the scanned range needs checks only
//               for fewer transfers. Ties in departure time are reported
//               together, so a page may hold more than n journeys.
enum class reversed_order : std::uint8_t { kArrival, kDeparture };

template <typename AlgoState>
routing_result reversed_search(
    timetable const&,
    rt_timetable const*,
    search_state&,
    AlgoState&,
    query,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt,
    reversed_order = reversed_order::kArrival);

}  // namespace nigiri::routing
