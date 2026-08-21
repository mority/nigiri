#pragma once

#include <array>
#include <optional>

#include "nigiri/routing/journey.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;

// Finds the best single-transit connection between `from` and `to`, i.e. one
// transport plus (possibly empty) footpaths at both ends. Nothing else in
// between - if `from` and `to` are not connected by a common route/rt-transport
// (modulo footpaths), `std::nullopt` is returned.
//
// kForward:  `from_time` is the arrival time at `from`, `to_time` is an upper
//            bound for the arrival at `to`. The earliest arrival is returned.
// kBackward: `from_time` is the departure time at `from`, `to_time` is a lower
//            bound for the departure at `to`. The latest departure is returned.
//
// The returned legs are in *search* order, i.e. index 0 is always the leg
// adjacent to `from` and index 2 the leg adjacent to `to`:
//   [0]  footpath from `from` to the stop where the transport is entered
//   [1]  the transport
//   [2]  footpath from the stop where the transport is exited to `to`
// The legs themselves are always oriented in travel direction (`from_`/`to_`
// and `dep_time_`/`arr_time_` are set accordingly), so for kBackward the array
// has to be reversed to obtain travel order.
//
// `opt` marks `from` / `to` as a journey terminal. This is off by default, so
// `pong` - which only ever asks about a hop *inside* an existing journey - is
// unaffected. The pattern-based search sets it for the first and last hop.
//
// At a terminal the passenger starts or ends their journey, so:
//   * no transfer time is charged for boarding/alighting there, and
//   * equivalent locations are reachable at zero cost, expanded with
//     `for_each_meta` and the query's match mode - the same thing `get_starts`
//     does for a normal RAPTOR query. Without this a parent station whose
//     routes live on child platforms is only reachable over the parent-child
//     footpath, which both delays boarding and shifts the reported times.
// Footpaths to genuinely separate stops still cost their duration either way.
//
// `is_src`/`is_dst` are scratch buffers, they are resized + cleared internally.
struct alternative_options {
  // `from` is where the journey begins (kForward) / ends (kBackward)
  bool from_is_terminal_{false};
  // `to` is where the journey ends (kForward) / begins (kBackward)
  bool to_is_terminal_{false};
};

template <direction SearchDir>
std::optional<std::array<journey::leg, 3U>> get_earliest_alternative(
    timetable const&,
    rt_timetable const*,
    query const&,
    location_idx_t from,
    location_idx_t to,
    unixtime_t from_time,
    unixtime_t to_time,
    bitvec& is_src,
    bitvec& is_dst,
    alternative_options opt = {});

}  // namespace nigiri::routing
