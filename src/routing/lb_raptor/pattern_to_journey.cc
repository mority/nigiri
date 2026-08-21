#include "nigiri/routing/lb_raptor/pattern_to_journey.h"

#include <algorithm>

#include "utl/overloaded.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/lb_raptor/get_alternative.h"
#include "nigiri/routing/query.h"
#include "nigiri/special_stations.h"
#include "nigiri/td_footpath.h"
#include "nigiri/timetable.h"

#define trace_lb(...)
// #define trace_lb fmt::println

namespace nigiri::routing {

namespace {

// Realizes `pattern` for one concrete anchor time: the passenger is at the
// search source (`q.start_` for kFwd, `q.destination_` for kBwd) at
// `start_time` and takes the earliest/latest transport on every hop.
template <direction SearchDir>
std::optional<journey> realize(timetable const& tt,
                               rt_timetable const* rtt,
                               query const& q,
                               std::vector<location_idx_t> const& pattern,
                               unixtime_t const start_time) {
  constexpr auto const kFwd = SearchDir == direction::kForward;

  if (pattern.size() < 2U) {
    trace_lb("[pattern_to_journey] pattern too short: {}", pattern.size());
    return std::nullopt;
  }

  // Time advances forward (kFwd) / backward (kBwd) in search direction.
  auto const adv = [](duration_t const d) { return kFwd ? d : -d; };

  // The pattern is given in travel order, but it is walked in search order:
  // `at(0)` is matched by `q.start_` (the search source), `at(n - 1)` by
  // `q.destination_`. For kBwd that means walking the pattern backwards.
  auto const n = pattern.size();
  auto const at = [&](std::size_t const i) {
    return kFwd ? pattern[i] : pattern[n - 1U - i];
  };

  // Earliest (kFwd) / latest (kBwd) time the journey may end at.
  auto const deadline = start_time + adv(q.max_travel_time_);

  auto const find_offset = [&](std::vector<offset> const& offsets,
                               td_offsets_t const& td_offsets,
                               location_match_mode const match_mode,
                               location_idx_t const l,
                               unixtime_t const t) -> std::optional<offset> {
    auto best = std::optional<offset>{};
    for (auto const& o : offsets) {
      if (o.duration() < footpath::kMaxDuration &&
          matches(tt, match_mode, o.target(), l) &&
          (!best.has_value() || o.duration() < best->duration())) {
        best = o;
      }
    }
    for (auto const& [x, tds] : td_offsets) {
      if (!matches(tt, match_mode, x, l)) {
        continue;
      }
      auto const d = get_td_duration<SearchDir>(tds, t);
      if (d.has_value() && d->first < footpath::kMaxDuration &&
          (!best.has_value() || d->first < best->duration())) {
        best = offset{x, d->first, d->second.transport_mode_id_};
      }
    }
    return best;
  };

  auto const start_offset = find_offset(
      q.start_, q.td_start_, q.start_match_mode_, at(0U), start_time);
  if (!start_offset.has_value()) {
    trace_lb("[pattern_to_journey] no start offset for {}",
          tt.get_default_name(at(0U)));
    return std::nullopt;
  }

  // Legs are collected in search direction and reversed at the end if
  // necessary - `journey::legs_` is always in travel order.
  auto legs = std::vector<journey::leg>{};
  auto cur = at(0U);
  auto cur_time = start_time + adv(start_offset->duration());

  if (q.start_match_mode_ == location_match_mode::kIntermodal) {
    legs.emplace_back(journey::leg{SearchDir,
                                   get_special_station(special_station::kStart),
                                   at(0U), start_time, cur_time,
                                   *start_offset});
  }

  auto n_transports = 0U;
  for (auto i = std::size_t{1U}; i != n; ++i) {
    auto const next = at(i);
    auto const is_last = (i + 1U == n);

    // The first and last hop touch the journey's terminals: no transfer time
    // there, and the whole station complex counts as the terminal. In between,
    // `cur` is a stop the passenger just got off at.
    auto const alt = get_alternative<SearchDir>(
        tt, rtt, q, cur, next, cur_time, deadline,
        alternative_options{.from_is_terminal_ = i == 1U,
                            .to_is_terminal_ = is_last});
    if (!alt.has_value()) {
      trace_lb("[pattern_to_journey] no transport {} -> {} after {}",
            tt.get_default_name(cur), tt.get_default_name(next), cur_time);
      return std::nullopt;
    }

    auto const& enter_fp = (*alt)[0];
    auto const& transport = (*alt)[1];
    auto const& exit_fp = (*alt)[2];

    // A self-transfer at the journey start is not needed: the passenger is
    // already there. Everywhere else the transfer time has to be respected.
    if (i != 1U || enter_fp.from_ != enter_fp.to_) {
      legs.emplace_back(enter_fp);
    }
    legs.emplace_back(transport);
    ++n_transports;

    if (is_last) {
      // A self-transfer at the journey end is not needed: the passenger has
      // arrived. A real footpath to the last pattern station is kept.
      if (exit_fp.from_ != exit_fp.to_) {
        legs.emplace_back(exit_fp);
        cur_time = kFwd ? exit_fp.arr_time_ : exit_fp.dep_time_;
      } else {
        cur_time = kFwd ? transport.arr_time_ : transport.dep_time_;
      }
      cur = next;
    } else {
      // Continue from the stop where the transport was left. The transfer to
      // the next pattern station is covered by the next enter footpath, so
      // `exit_fp` is intentionally dropped here (it would count the transfer
      // time at the transfer station twice).
      cur = kFwd ? transport.to_ : transport.from_;
      cur_time = kFwd ? transport.arr_time_ : transport.dep_time_;
    }
  }

  auto const dest_offset = find_offset(q.destination_, q.td_dest_,
                                       q.dest_match_mode_, at(n - 1U),
                                       cur_time);
  if (!dest_offset.has_value()) {
    trace_lb("[pattern_to_journey] no destination offset for {}",
          tt.get_default_name(at(n - 1U)));
    return std::nullopt;
  }

  auto const dest_time = cur_time + adv(dest_offset->duration());
  if (kFwd ? dest_time > deadline : dest_time < deadline) {
    trace_lb("[pattern_to_journey] exceeds max_travel_time: {} vs {}", dest_time,
          deadline);
    return std::nullopt;
  }

  if (q.dest_match_mode_ == location_match_mode::kIntermodal) {
    legs.emplace_back(journey::leg{SearchDir, at(n - 1U),
                                   get_special_station(special_station::kEnd),
                                   cur_time, dest_time, *dest_offset});
  }

  if constexpr (!kFwd) {
    std::reverse(begin(legs), end(legs));
  }

  auto j = journey{};
  j.legs_ = std::move(legs);
  j.start_time_ = start_time;
  j.dest_time_ = dest_time;
  j.dest_ = at(n - 1U);
  j.transfers_ = static_cast<std::uint8_t>(n_transports - 1U);
  // The legs are built here directly, there is no separate reconstruct step.
  j.is_reconstructed_ = true;
  return j;
}

// The anchor the journey was realized for is only a lower (kFwd) / upper (kBwd)
// bound on when the passenger has to set off: the first transport usually
// departs later. Shift the access legs so they end exactly at it and report
// that as `start_time_`, which makes the journey comparable to what range
// RAPTOR produces and lets the caller step to the next distinct departure.
template <direction SearchDir>
void tighten_to_first_transport(journey& j) {
  constexpr auto const kFwd = SearchDir == direction::kForward;

  auto const is_transport = [](journey::leg const& l) {
    return std::holds_alternative<journey::run_enter_exit>(l.uses_);
  };

  // `legs_` is in travel order; the search source is at the front for kFwd and
  // at the back for kBwd.
  auto const n = j.legs_.size();
  auto n_access = std::size_t{0U};
  while (n_access != n && !is_transport(j.legs_[kFwd ? n_access
                                                     : n - n_access - 1U])) {
    ++n_access;
  }
  if (n_access == n) {
    return;  // no transport at all
  }

  auto t = kFwd ? j.legs_[n_access].dep_time_
                : j.legs_[n - n_access - 1U].arr_time_;
  for (auto i = std::size_t{0U}; i != n_access; ++i) {
    auto& l = j.legs_[kFwd ? n_access - i - 1U : n - n_access + i];
    auto const d = l.arr_time_ - l.dep_time_;
    if constexpr (kFwd) {
      l.arr_time_ = t;
      l.dep_time_ = t - d;
      t = l.dep_time_;
    } else {
      l.dep_time_ = t;
      l.arr_time_ = t + d;
      t = l.arr_time_;
    }
  }
  j.start_time_ = t;
}

}  // namespace

template <direction SearchDir>
std::optional<journey> pattern_to_journey(
    timetable const& tt,
    rt_timetable const* rtt,
    query const& q,
    std::vector<location_idx_t> const& pattern) {
  constexpr auto const kFwd = SearchDir == direction::kForward;
  auto const start_time = std::visit(
      utl::overloaded{[](unixtime_t const t) { return t; },
                      [](interval<unixtime_t> const i) {
                        // `to_` is exclusive
                        return kFwd ? i.from_ : i.to_ - duration_t{1};
                      }},
      q.start_time_);
  return realize<SearchDir>(tt, rtt, q, pattern, start_time);
}

template <direction SearchDir>
void pattern_to_journeys(timetable const& tt,
                         rt_timetable const* rtt,
                         query const& q,
                         std::vector<location_idx_t> const& pattern,
                         interval<unixtime_t> const search_interval,
                         std::vector<journey>& out) {
  constexpr auto const kFwd = SearchDir == direction::kForward;

  // Walk the interval in search direction, one distinct departure (kFwd) /
  // arrival (kBwd) at a time - the same idea as range RAPTOR, but the pattern
  // is fixed so no search is repeated, only the realization.
  auto t = kFwd ? search_interval.from_ : search_interval.to_ - duration_t{1};
  while (search_interval.contains(t)) {
    auto j = realize<SearchDir>(tt, rtt, q, pattern, t);
    if (!j.has_value()) {
      // Nothing departs after `t` any more (or the pattern is unusable).
      break;
    }

    tighten_to_first_transport<SearchDir>(*j);
    if (!search_interval.contains(j->start_time_)) {
      // The first transport is already outside the interval.
      break;
    }

    auto const next = j->start_time_ + (kFwd ? duration_t{1} : duration_t{-1});
    out.emplace_back(std::move(*j));
    t = next;
  }
}

template std::optional<journey> pattern_to_journey<direction::kForward>(
    timetable const&,
    rt_timetable const*,
    query const&,
    std::vector<location_idx_t> const&);
template std::optional<journey> pattern_to_journey<direction::kBackward>(
    timetable const&,
    rt_timetable const*,
    query const&,
    std::vector<location_idx_t> const&);

template void pattern_to_journeys<direction::kForward>(
    timetable const&,
    rt_timetable const*,
    query const&,
    std::vector<location_idx_t> const&,
    interval<unixtime_t>,
    std::vector<journey>&);
template void pattern_to_journeys<direction::kBackward>(
    timetable const&,
    rt_timetable const*,
    query const&,
    std::vector<location_idx_t> const&,
    interval<unixtime_t>,
    std::vector<journey>&);

}  // namespace nigiri::routing
