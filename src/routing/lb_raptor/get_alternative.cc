#include "nigiri/routing/lb_raptor/get_alternative.h"

#include <algorithm>

#include "utl/helpers/algorithm.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/direct.h"
#include "nigiri/routing/leg_alternatives.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

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
  auto direct_query = make_alternative_query(tt, rtt, q, boarding, alighting);

  // A terminal must be matched exactly the way the *query* defines it - not by
  // re-expanding the pattern node. `bidir_lb_raptor::init` seeds the search at
  // station complexes, so the node is the complex of some location the query
  // named; the locations that may legitimately be boarded/alighted here are
  // therefore `{m in the query's own expansion : complex(m) == node}`.
  //
  // Expanding the node itself instead (with `kEquivalent`) pulls in
  // `equivalences_[node]`, which are separate stations - in a GTFS feed
  // typically the neighbouring stop of the same name, tens or hundreds of
  // metres away - and hands them the terminal's offset, zero. That produced
  // access legs like "walk 4 min to Stadtpalais, then 0 s to Aschaffenburg
  // Hbf", and journeys that end at a station the query never named (one stop
  // short of the requested destination, arriving a minute earlier than the
  // true optimum). Stations outside the sanctioned set stay reachable through
  // the query's footpaths, at their real duration.
  //
  // `kOnlyChildren` on the sanctioned targets keeps their platforms boardable
  // (a parent station carries no routes of its own) without crossing to
  // another station.
  auto const terminal_offsets = [&](std::vector<offset> const& offsets,
                                    td_offsets_t const& td_offsets,
                                    location_match_mode const mode,
                                    location_idx_t const node) {
    auto out = std::vector<offset>{};
    auto const add = [&](location_idx_t const m) {
      if (tt.get_complex_idx(m) == node &&
          utl::find_if(out, [&](offset const& o) { return o.target() == m; }) ==
              end(out)) {
        out.emplace_back(m, duration_t{0}, transport_mode_id_t{0});
      }
    };
    for (auto const& o : offsets) {
      for_each_meta(tt, mode, o.target(), add);
    }
    for (auto const& [l, tds] : td_offsets) {
      for_each_meta(tt, mode, l, add);
    }
    return out;
  };

  // `from` is the search source (`q.start_`), `to` the search target
  // (`q.destination_`); `direct_query` is always in travel order.
  auto const apply_terminal = [&](bool const is_from,
                                  location_idx_t const node) {
    auto sub = terminal_offsets(
        is_from ? q.start_ : q.destination_, is_from ? q.td_start_ : q.td_dest_,
        is_from ? q.start_match_mode_ : q.dest_match_mode_, node);
    if (sub.empty()) {
      // the node is not covered by the query's expansion (it can only be the
      // complex of a location that is) - fall back to the node itself
      sub.emplace_back(node, duration_t{0}, transport_mode_id_t{0});
    }
    if (is_from == kFwd) {
      direct_query.start_ = std::move(sub);
      direct_query.start_match_mode_ = location_match_mode::kOnlyChildren;
    } else {
      direct_query.destination_ = std::move(sub);
      direct_query.dest_match_mode_ = location_match_mode::kOnlyChildren;
    }
  };

  if (opt.from_is_terminal_) {
    apply_terminal(true, from);
  }
  if (opt.to_is_terminal_) {
    apply_terminal(false, to);
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
