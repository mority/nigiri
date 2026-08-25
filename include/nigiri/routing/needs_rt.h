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

// The start times `q` asks for, without any interval extension. Half-open and
// never empty: a single start time becomes a one minute interval.
inline interval<unixtime_t> query_start_interval(query const& q) {
  return std::visit(
      utl::overloaded{
          [](unixtime_t const t) { return interval{t, t + i32_minutes{1}}; },
          [](interval<unixtime_t> const i) {
            return interval{i.from_, std::max(i.to_, i.from_ + i32_minutes{1})};
          }},
      q.start_time_);
}

// Decides whether `rtt` can influence the result of a query at all. If it
// cannot, the query can be answered from the static timetable alone - the
// caller can simply drop the real-time timetable pointer, which enables the
// `Rt = false` specialization of the algorithm and skips all real-time lookups
// in the lower bound computation, the start label generation, the routing
// itself and the reconstruction.
//
// `start_itv` is every start time the search can ever try (including the ones
// the interval extension can reach) and `max_reach` is how far a search
// started at one of them can look in search direction. The decision is made by
// intersecting `rtt.coverage_` (see `rt_timetable.h`) with the resulting
// interval of event times. That is sound in both directions because
// `coverage_` contains the scheduled *and* the updated times of every
// transport a real-time update touched:
//   - a real-time update can only *add* a journey if its updated event times
//     are inside that interval,
//   - and it can only *remove* or *worsen* a journey that the static search
//     finds if that journey's scheduled event times are inside it.
template <direction SearchDir>
bool needs_rt(rt_timetable const& rtt,
              query const& q,
              interval<unixtime_t> const& start_itv,
              duration_t const max_reach) {
  // Time dependent footpaths (e.g. elevator status) are written from outside
  // the real-time update pipeline and are therefore not tracked by
  // `coverage_`. They are only read for profiles != 0.
  if (q.prf_idx_ != 0U) {
    return true;
  }

  if (!rtt.has_coverage()) {
    return false;
  }

  // Computed in 64 bit minutes: `unixtime_t` is 32 bit minutes and would
  // overflow when `max_reach` is added to a start time at the end of its
  // range.
  auto const min_since_epoch = [](unixtime_t const t) {
    return static_cast<std::int64_t>(t.time_since_epoch().count());
  };

  auto const reach = std::int64_t{max_reach.count()} + 1;
  auto const rel_from = SearchDir == direction::kForward
                            ? min_since_epoch(start_itv.from_)
                            : min_since_epoch(start_itv.from_) - reach;
  auto const rel_to = SearchDir == direction::kForward
                          ? min_since_epoch(start_itv.to_) + reach
                          : min_since_epoch(start_itv.to_);

  // Both intervals are half-open.
  return rel_from < min_since_epoch(rtt.coverage_.to_) &&
         min_since_epoch(rtt.coverage_.from_) < rel_to;
}

// `needs_rt()` for the range RAPTOR search (`search.h`), whose start times are
// bounded by the interval estimator.
//
// `q` must be sanitized (`query::sanitize()`) - `max_travel_time_` is relied
// upon to be within `kMaxTravelTime`.
template <direction SearchDir>
bool needs_rt(timetable const& tt, rt_timetable const& rtt, query const& q) {
  // Every start time the search can ever try: the query interval itself
  // (`interval_estimator::initial()` returns it unchanged if the interval is
  // not extended) united with everything the interval extension can reach.
  auto const query_itv = query_start_interval(q);
  auto const max_itv = interval_estimator<SearchDir>{tt, q}.max_interval();
  auto const start_itv = interval{std::min(query_itv.from_, max_itv.from_),
                                  std::max(query_itv.to_, max_itv.to_)};

  // Journeys never reach further than `max_travel_time_` from their start
  // time: `search_interval()` prunes with `worst_time_at_dest` and everything
  // longer is erased from the results. Start/destination offsets are part of
  // the travel time, so intermodal queries are covered as well.
  return needs_rt<SearchDir>(rtt, q, start_itv, q.max_travel_time_);
}

// `needs_rt()` for the PONG search (`raptor/pong.h`).
//
// PONG does not use the interval estimator: it walks the start time from the
// query interval in search direction, one found journey at a time, and stops
// as soon as it has enough connections or a ping comes up empty. In practice
// that ends a few departures past the query interval - but nothing bounds it
// statically (the number of iterations cannot be derived from
// `min_connection_count_`: `pareto_set::add()` may drop an already counted
// journey again), so the worst case is the whole external interval of the
// timetable.
//
// It also looks `min_look_ahead` further than `max_travel_time_` (see
// `worst_time_at_dest` in `pong()`) - journeys found out there are erased
// again, but they are visited, so the window is widened by it to stay on the
// safe side.
//
// `q` must be sanitized (`query::sanitize()`).
template <direction SearchDir>
bool pong_needs_rt(timetable const& tt,
                   rt_timetable const& rtt,
                   query const& q,
                   duration_t const min_look_ahead) {
  auto const query_itv = query_start_interval(q);
  auto const external_itv = tt.external_interval();
  auto const start_itv =
      SearchDir == direction::kForward
          ? interval{query_itv.from_, std::max(query_itv.to_, external_itv.to_)}
          : interval{std::min(query_itv.from_, external_itv.from_),
                     query_itv.to_};

  return needs_rt<SearchDir>(
      rtt, q, start_itv,
      std::min(q.max_travel_time_ + min_look_ahead, kMaxTravelTime));
}

}  // namespace nigiri::routing
