#pragma once

#include <cstdint>
#include <algorithm>
#include <variant>

#include "utl/overloaded.h"

#include "nigiri/routing/interval_estimate.h"
#include "nigiri/routing/query.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

// Decides whether `rtt` can influence the result of `q` at all. If it cannot,
// the query can be answered from the static timetable alone - the caller can
// simply drop the real-time timetable pointer, which enables the `Rt = false`
// specialization of the algorithm and skips all real-time lookups in the lower
// bound computation, the start label generation, the routing itself and the
// reconstruction.
//
// The decision is made by intersecting `rtt.coverage_` (see `rt_timetable.h`)
// with the interval of event times that can end up in a result of `q`. That is
// sound in both directions because `coverage_` contains the scheduled *and*
// the updated times of every transport a real-time update touched:
//   - a real-time update can only *add* a journey if its updated event times
//     are inside the query's interval,
//   - and it can only *remove* or *worsen* a journey that the static search
//     finds if that journey's scheduled event times are inside it.
//
// `q` must be sanitized (`query::sanitize()`) - `max_travel_time_` is relied
// upon to be within `kMaxTravelTime`.
template <direction SearchDir>
bool needs_rt(timetable const& tt, rt_timetable const& rtt, query const& q) {
  // Time dependent footpaths (e.g. elevator status) are written from outside
  // the real-time update pipeline and are therefore not tracked by
  // `coverage_`. They are only read for profiles != 0.
  if (q.prf_idx_ != 0U) {
    return true;
  }

  if (!rtt.has_coverage()) {
    return false;
  }

  // Every start time the search can ever try: the query interval itself
  // (`interval_estimator::initial()` returns it unchanged if the interval is
  // not extended) united with everything the interval extension can reach.
  auto const query_itv = std::visit(
      utl::overloaded{
          [](unixtime_t const t) { return interval{t, t + i32_minutes{1}}; },
          [](interval<unixtime_t> const i) {
            return interval{i.from_, std::max(i.to_, i.from_ + i32_minutes{1})};
          }},
      q.start_time_);
  auto const max_itv = interval_estimator<SearchDir>{tt, q}.max_interval();

  // Computed in 64 bit minutes: `unixtime_t` is 32 bit minutes and would
  // overflow when `max_travel_time_` is added to a start time at the end of
  // its range.
  auto const min_since_epoch = [](unixtime_t const t) {
    return static_cast<std::int64_t>(t.time_since_epoch().count());
  };

  auto const from = std::min(min_since_epoch(query_itv.from_),
                             min_since_epoch(max_itv.from_));
  auto const to =
      std::max(min_since_epoch(query_itv.to_), min_since_epoch(max_itv.to_));

  // Journeys never reach further than `max_travel_time_` from their start
  // time: `search_interval()` prunes with `worst_time_at_dest` and everything
  // longer is erased from the results. Start/destination offsets are part of
  // the travel time, so intermodal queries are covered as well.
  auto const max_travel_time = std::int64_t{q.max_travel_time_.count()} + 1;
  auto const rel_from =
      SearchDir == direction::kForward ? from : from - max_travel_time;
  auto const rel_to =
      SearchDir == direction::kForward ? to + max_travel_time : to;

  // Both intervals are half-open.
  return rel_from < min_since_epoch(rtt.coverage_.to_) &&
         min_since_epoch(rtt.coverage_.from_) < rel_to;
}

}  // namespace nigiri::routing
