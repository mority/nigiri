#include "nigiri/routing/get_earliest_alternative.h"

#include <algorithm>

#include "nigiri/routing/direct.h"
#include "nigiri/routing/leg_alternatives.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

std::optional<std::array<journey::leg, 3U>> get_earliest_alternative(
    timetable const& tt,
    rt_timetable const* rtt,
    query const& q,
    location_idx_t const from,
    location_idx_t const to,
    unixtime_t const from_arr,
    unixtime_t const to_dep) {
  auto const direct_query = make_alternative_query(tt, rtt, q, from, to);
  auto cursor =
      get_direct_journeys<direction::kForward>(tt, rtt, direct_query, from_arr);
  if (!cursor) {
    return std::nullopt;
  }
  auto legs = cursor();
  if (legs.back().arr_time_ > to_dep) {
    return std::nullopt;
  }
  // the generator anchors the boarding walk at the transit departure
  // (latest start) -> shift to the interior transfer convention:
  // the walk starts at the previous leg's arrival
  auto const walk_duration = legs[0].arr_time_ - legs[0].dep_time_;
  legs[0].dep_time_ = from_arr;
  legs[0].arr_time_ = from_arr + walk_duration;
  return std::array{std::move(legs[0]), std::move(legs[1]), std::move(legs[2])};
}

template <direction SearchDir>
std::optional<std::array<journey::leg, 3U>> get_alternative(
    timetable const& tt,
    rt_timetable const* rtt,
    query const& q,
    location_idx_t const from,
    location_idx_t const to,
    unixtime_t const from_time,
    unixtime_t const to_time,
    alternative_options const opt) {
  constexpr auto const kFwd = SearchDir == direction::kForward;

  // `get_direct_journeys` always takes the query in travel order: `start_` is
  // the boarding side, `destination_` the alighting side. `SearchDir` only
  // decides whether it walks forward from `from_time` (earliest departure) or
  // backward from it (latest arrival). For kBackward `from` is the later end of
  // the connection, so the two sides swap.
  auto const boarding = kFwd ? from : to;
  auto const alighting = kFwd ? to : from;
  auto direct_query =
      make_alternative_query(tt, rtt, q, boarding, alighting);

  // A terminal stands for its whole station complex.
  auto const boarding_terminal =
      kFwd ? opt.from_is_terminal_ : opt.to_is_terminal_;
  auto const alighting_terminal =
      kFwd ? opt.to_is_terminal_ : opt.from_is_terminal_;
  if (boarding_terminal) {
    direct_query.start_match_mode_ = q.start_match_mode_;
  }
  if (alighting_terminal) {
    direct_query.dest_match_mode_ = q.dest_match_mode_;
  }

  auto cursor =
      get_direct_journeys<SearchDir>(tt, rtt, direct_query, from_time);
  if (!cursor) {
    return std::nullopt;
  }

  auto legs = cursor();
  if (legs.size() != 3U) {
    return std::nullopt;
  }

  // `legs` is in travel order; the bound applies at the `to` end.
  auto const time_at_to = kFwd ? legs.back().arr_time_ : legs.front().dep_time_;
  if (kFwd ? time_at_to > to_time : time_at_to < to_time) {
    return std::nullopt;
  }

  if constexpr (kFwd) {
    return std::array{std::move(legs[0]), std::move(legs[1]),
                      std::move(legs[2])};
  } else {
    // search order: index 0 adjacent to `from`
    return std::array{std::move(legs[2]), std::move(legs[1]),
                      std::move(legs[0])};
  }
}

template std::optional<std::array<journey::leg, 3U>>
get_alternative<direction::kForward>(timetable const&,
                                     rt_timetable const*,
                                     query const&,
                                     location_idx_t,
                                     location_idx_t,
                                     unixtime_t,
                                     unixtime_t,
                                     alternative_options);

template std::optional<std::array<journey::leg, 3U>>
get_alternative<direction::kBackward>(timetable const&,
                                      rt_timetable const*,
                                      query const&,
                                      location_idx_t,
                                      location_idx_t,
                                      unixtime_t,
                                      unixtime_t,
                                      alternative_options);

}  // namespace nigiri::routing
