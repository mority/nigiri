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

// Marks `from` / `to` as a journey terminal. At a terminal the passenger starts
// or ends their journey, so it is matched the way the *query* defines that
// terminal: the boardable/alightable locations are the ones from the query's
// own expansion that belong to this station, instead of an exact match on the
// pattern node. Everything else is reached through footpaths, at its real
// duration.
// Off by default.
struct alternative_options {
  // `from` is where the journey begins (kForward) / ends (kBackward)
  bool from_is_terminal_{false};
  // `to` is where the journey ends (kForward) / begins (kBackward)
  bool to_is_terminal_{false};
};

// Single-transit connection between `from` and `to`: one transport plus
// (possibly empty) walks at both ends, enumerated by `get_direct_journeys`.
// `from`/`to` are consecutive stations of a transfer pattern.
//
// This is the pattern-search counterpart to pong's `get_earliest_alternative`
// (see raptor/pong.h): same underlying generator, but direction-generic and
// with the terminal handling below.
//
// kForward:  `from_time` is the arrival at `from`, `to_time` an upper bound for
//            the arrival at `to`; the earliest connection is returned.
// kBackward: `from_time` is the departure at `from`, `to_time` a lower bound
//            for the departure at `to`; the latest connection is returned.
//
// The returned legs are in *search* order - index 0 is always adjacent to
// `from` - while each leg itself is oriented in travel direction. The boarding
// walk stays anchored at the transport (i.e. the passenger leaves as late as
// possible), which is what the pattern search needs to step to the next
// distinct departure. (pong's variant shifts it back to the arrival of the
// preceding transport, because there it replaces an interior transfer.)
template <direction SearchDir>
std::optional<std::array<journey::leg, 3U>> get_alternative(
    timetable const&,
    rt_timetable const*,
    query const&,
    location_idx_t from,
    location_idx_t to,
    unixtime_t from_time,
    unixtime_t to_time,
    alternative_options opt = {});

}  // namespace nigiri::routing
