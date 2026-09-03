#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include <vector>

#include "utl/get_or_create.h"
#include "utl/helpers/algorithm.h"
#include "utl/overloaded.h"
#include "utl/pipes/remove_if.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/lb_raptor/pattern_to_journey.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include <ranges>
#include "utl/enumerate.h"

#include "utl/erase_if.h"
#include "utl/to_vec.h"

#define trace_lb(...)
// #define trace_lb fmt::println

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

// A pattern only has so many distinct departures, and each pass costs a full
// realization of every surviving pattern - this is the safety net, not a
// tuning knob.
constexpr auto kMaxRealizationPasses = 32U;

void bidir_lb_raptor::reset(unsigned const n_locations,
                            unsigned const n_lb_routes) {
  auto const reset_round_times = [&](auto& round_times) {
    for (auto& a : round_times) {
      a.resize(n_locations);
      utl::fill(a, kUnreachable);
    }
  };
  reset_round_times(fwd_round_times_);
  reset_round_times(bwd_round_times_);

  tmp_.resize(n_locations);
  utl::fill(tmp_, kUnreachable);

  auto const reset_bitvec = [&](auto& v, auto const size) {
    v.resize(size);
    utl::fill(v.blocks_, 0U);
  };

  reset_bitvec(fwd_station_mark_, n_locations);
  reset_bitvec(bwd_station_mark_, n_locations);
  reset_bitvec(prev_station_mark_, n_locations);
  reset_bitvec(fwd_reached_, n_locations);
  reset_bitvec(bwd_reached_, n_locations);
  reset_bitvec(is_start_, n_locations);
  reset_bitvec(is_dest_, n_locations);
  reset_bitvec(lb_route_mark_, n_lb_routes);

  journeys_.clear();
  journey_index_.clear();
  new_journeys_.clear();

  meetpoints_.clear();
  meetpoints_.reserve(1000);

  patterns_.clear();
  realizable_.clear();
  pattern_of_.clear();
  stats_.reset();
}

template <direction SearchDir>
void bidir_lb_raptor::init(timetable const& tt,
                           query const& q,
                           bool const arrive_by) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  // The lb routes are traversed in travel direction by run<kForward>, so the
  // forward search always has to start at the *travel* origin. For arrive_by
  // queries (which are flipped, see query::flip_dir) that is `q.destination_`.
  auto const use_start = (kFwd != arrive_by);
  auto const& offsets = use_start ? q.start_ : q.destination_;
  auto const& td_offsets = use_start ? q.td_start_ : q.td_dest_;
  auto const match_mode = use_start ? q.start_match_mode_ : q.dest_match_mode_;
  auto& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;
  auto& reached = kFwd ? fwd_reached_ : bwd_reached_;

  min_.clear();
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const cplx = tt.get_complex_idx(l);
    auto& m =
        utl::get_or_create(min_, cplx, [&] { return round_times[0][cplx]; });
    m = std::min(d, m);
  };

  for (auto const& o : offsets) {
    for_each_meta(tt, match_mode, o.target(), [&](location_idx_t const l) {
      update_min(l, static_cast<std::uint16_t>(o.duration().count()));
    });
  }

  for (auto const& [l, tds] : td_offsets) {
    for (auto const& td : tds) {
      if (td.duration() != footpath::kMaxDuration &&
          td.duration() < q.max_travel_time_) {
        update_min(l, static_cast<std::uint16_t>(td.duration().count()));
      }
    }
  }

  // `min_` is keyed by cplx location and already covers everything the query's
  // own match mode reaches from the offset targets - the same single expansion
  // `get_starts` / `collect_destinations` do for RAPTOR. Expanding those roots
  // a *second* time would add `equivalences_[cplx]` (pass 1 only sees
  // `equivalences_[target]`, which is empty for a platform target), and every
  // location marked here can terminate a pattern: the journey would then start
  // or end at a station the query never named.
  for (auto const& [l, t] : min_) {
    round_times[0][l] = std::min(t, round_times[0][l]);
    station_mark.set(to_idx(l), true);
    reached.set(to_idx(l), true);
    if constexpr (kFwd) {
      is_start_.set(to_idx(l), true);
    } else {
      is_dest_.set(to_idx(l), true);
    }
  }
}

template <direction SearchDir>
bool bidir_lb_raptor::run(timetable const& tt,
                          query const& q,
                          unsigned const k) {
  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_complex_seq = tt.lb_route_complex_seq_[q.prf_idx_];

  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;
  auto& reached = kFwd ? fwd_reached_ : bwd_reached_;
  auto& rev_reached = kFwd ? bwd_reached_ : fwd_reached_;

  for (auto const l :
       interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
    round_times[k][l] = round_times[k - 1U][l];
  }
  utl::fill(tmp_, kUnreachable);

  auto any_marked = false;
  station_mark.for_each_set_bit([&](std::uint64_t const i) {
    for (auto const r : routes[location_idx_t{i}]) {
      any_marked = true;
      lb_route_mark_.set(to_idx(r), true);
    }
  });
  if (!any_marked) {
    return false;
  }

  std::swap(prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  any_marked = false;
  lb_route_mark_.for_each_set_bit([&](auto const i) {
    auto const r = lb_route_idx_t{i};
    auto const& segment_layovers = route_times[r];
    auto const& seq = route_complex_seq[r];

    for (auto x = 0U; x != seq.size(); ++x) {
      auto const in = kFwd ? x : seq.size() - x - 1U;
      auto const l_in = seq[in];

      if (!prev_station_mark_.test(to_idx(l_in))) {
        continue;
      }

      auto lb = round_times[k - 1U][l_in];
      auto const step = kFwd ? 1 : -1;
      for (auto out = static_cast<std::int32_t>(in + step);
           0 <= out && out < static_cast<std::int32_t>(seq.size());
           out += step) {
        auto const l_out = seq[out];
        auto const segment =
            kFwd ? segment_layovers[(out - 1) * 2] : segment_layovers[out * 2];

        lb += segment.count();
        if (lb < std::min(round_times[k][l_out], tmp_[l_out])) {
          tmp_[l_out] = lb;
          station_mark.set(to_idx(l_out), true);
          reached.set(to_idx(l_out), true);
          any_marked = true;
        }

        // Riding past a terminal is never useful: the passenger has arrived
        // (kFwd) / has not set off yet (kBwd), so everything this route reaches
        // beyond it can only be a detour that comes back. Stopping here keeps
        // those labels - and the meetpoints, patterns and journeys built on
        // them - from ever being created.
        if ((kFwd ? is_dest_ : is_start_).test(to_idx(l_out))) {
          break;
        }

        // Keep scanning even when this stop was not improved: `lb` grows
        // monotonically along the route, but the bounds it is compared against
        // do not, so a stop further along may still be improvable. Aborting
        // here loses whole branches of the network - e.g. a route reaching
        // Ostkreuz past a stop that some other route already covers better.
        if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
          lb += segment_layovers[out * 2 - 1].count();
        } else {
          break;
        }
      }
    }
  });
  if (!any_marked) {
    return false;
  }

  utl::fill(lb_route_mark_.blocks_, 0U);

  std::swap(prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const time_after_transfer =
        tmp_[l] +
        adjusted_transfer_time(q.transfer_time_settings_,
                               tt.locations_.transfer_time_[l].count());

    // `tmp_[l]` was relaxed against the *pre*-transfer bound, so adding the
    // transfer time can make it worse than the label carried over from the
    // previous round. Writing it unconditionally would (a) break monotonicity
    // of `round_times` and (b) leave a label that `reconstruct` cannot invert,
    // because it is neither `tmp_[l] + transfer` of this round nor the value
    // of round k-1. Only keep it if it actually improves.
    if (time_after_transfer < round_times[k][l]) {
      round_times[k][l] = time_after_transfer;
      station_mark.set(i, true);
      reached.set(i, true);
    }
  });

  prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const expand_fps = [&](auto const x) {
      for (auto const fp : kFwd ? tt.locations_.footpaths_out_[q.prf_idx_][x]
                                : tt.locations_.footpaths_in_[q.prf_idx_][x]) {
        auto const cplx = tt.get_complex_idx(fp.target());
        auto const time_after_fp =
            tmp_[l] + adjusted_transfer_time(q.transfer_time_settings_,
                                             fp.duration().count());
        if (time_after_fp < round_times[k][cplx]) {
          round_times[k][cplx] = time_after_fp;
          station_mark.set(to_idx(cplx), true);
          reached.set(to_idx(cplx), true);
        }
      }
    };

    expand_fps(l);
    for (auto const c : tt.locations_.children_[l]) {
      expand_fps(c);
      for (auto const cc : tt.locations_.children_[c]) {
        expand_fps(cc);
      }
    }
  });

  if (!any_marked) {
    return false;
  }

  station_mark.for_each_set_bit([&](auto const i) {
    if (rev_reached.test(i)) {
      station_mark.set(i, false);
      auto const l = location_idx_t{i};
      if (utl::find_if(meetpoints_, [&](auto const m) {
            return matches(tt, location_match_mode::kEquivalent, l, m);
          }) == end(meetpoints_)) {
        meetpoints_.push_back(l);
      }
    }
  });

  return station_mark.any();
}

void bidir_lb_raptor::drop_destination_passthrough(timetable const& tt) {
  auto const passes_dest = [&](journey const& j) {
    auto const is_transit = [](journey::leg const& l) {
      return std::holds_alternative<journey::run_enter_exit>(l.uses_);
    };
    auto const last_transit = static_cast<std::size_t>(std::distance(
        begin(j.legs_),
        std::prev(
            utl::find_if(std::views::reverse(j.legs_), is_transit).base())));

    for (auto const [i, l] : utl::enumerate(j.legs_)) {
      auto const* const ree = std::get_if<journey::run_enter_exit>(&l.uses_);
      if (ree == nullptr) {
        continue;
      }
      auto const t_idx = ree->r_.t_.t_idx_;
      if (t_idx == transport_idx_t::invalid()) {
        continue;
      }
      // The boarding stop is where the passenger already is, and the very last
      // alighting stop is where the journey legitimately ends - everything in
      // between is riding *through* the destination.
      auto const& seq = tt.route_location_seq_[tt.transport_route_[t_idx]];
      auto const end_stop =
          i == last_transit ? static_cast<stop_idx_t>(ree->stop_range_.to_ - 1U)
                            : ree->stop_range_.to_;
      for (auto s = static_cast<stop_idx_t>(ree->stop_range_.from_ + 1U);
           s != end_stop; ++s) {
        if (is_dest_.test(
                to_idx(tt.get_complex_idx(stop{seq[s]}.location_idx())))) {
          return true;
        }
      }
    }
    return false;
  };

  auto const n_before = journeys_.size();
  utl::erase_if(journeys_, passes_dest);
  stats_.passthrough_journeys_ += n_before - journeys_.size();
}

void bidir_lb_raptor::add_journey(journey&& j) {
  auto k = key_of(j);
  auto const it = journey_index_.find(k);
  if (it == end(journey_index_)) {
    journey_index_.emplace(std::move(k), journeys_.size());
    journeys_.emplace_back(std::move(j));
    return;
  }
  ++stats_.duplicate_journeys_;
  // same vehicles: keep the tightest way of using them
  auto& kept = journeys_[it->second];
  auto const span_j = span(j), span_kept = span(kept);
  if (span_j < span_kept ||
      (span_j == span_kept && walk_time(j) < walk_time(kept))) {
    kept = std::move(j);
  }
}

bool bidir_lb_raptor::realize_next_departures(timetable const& tt,
                                              rt_timetable const* rtt,
                                              query const& q,
                                              bool const arrive_by) {
  // only patterns that still have a journey in the result are worth another
  // departure - a pattern whose journeys were all filtered away produces
  // nothing better later on
  auto alive = std::set<std::size_t>{};
  for (auto const& j : journeys_) {
    auto const it = pattern_of_.find(key_of(j));
    if (it != end(pattern_of_)) {
      alive.insert(it->second);
    }
  }

  auto const step = arrive_by ? duration_t{-1} : duration_t{1};
  auto added = false;
  for (auto const i : alive) {
    auto& r = realizable_[i];
    auto j = arrive_by ? pattern_to_journey_at<direction::kBackward>(
                             tt, rtt, q, r.pattern_, r.next_anchor_)
                       : pattern_to_journey_at<direction::kForward>(
                             tt, rtt, q, r.pattern_, r.next_anchor_);
    if (!j.has_value()) {
      continue;  // nothing departs after this any more
    }
    r.next_anchor_ = j->start_time_ + step;

    pattern_of_.emplace(key_of(*j), i);
    auto const n_before = journeys_.size();
    add_journey(std::move(*j));
    added = added || journeys_.size() != n_before;
  }
  return added;
}

void bidir_lb_raptor::drop_terminal_detours(timetable const& tt) {
  auto const complex_of = [&](location_idx_t const l) {
    return to_idx(tt.get_complex_idx(l));
  };
  auto const is_transit = [](journey::leg const& l) {
    return std::holds_alternative<journey::run_enter_exit>(l.uses_);
  };

  auto const detours = [&](journey const& j) {
    auto const first = utl::find_if(j.legs_, is_transit);
    if (first == end(j.legs_)) {
      return false;
    }
    auto const last = std::prev(
        utl::find_if(std::views::reverse(j.legs_), is_transit).base());

    // walked past another stop of the start to board where one could have
    // walked to directly
    auto const board = complex_of(first->from_);
    if (is_start_.test(board)) {
      for (auto it = begin(j.legs_); it != first; ++it) {
        if (complex_of(it->to_) != board &&
            is_start_.test(complex_of(it->to_))) {
          return true;
        }
      }
    }

    // same on the way out
    auto const alight = complex_of(last->to_);
    if (is_dest_.test(alight)) {
      for (auto it = std::next(last); it != end(j.legs_); ++it) {
        if (complex_of(it->from_) != alight &&
            is_dest_.test(complex_of(it->from_))) {
          return true;
        }
      }
    }

    return false;
  };

  auto const n_before = journeys_.size();
  utl::erase_if(journeys_, detours);
  stats_.terminal_detours_ += n_before - journeys_.size();
}

void bidir_lb_raptor::drop_same_route_reboarding(timetable const& tt) {
  auto const reboards = [&](journey const& j) {
    auto prev = static_cast<journey::run_enter_exit const*>(nullptr);
    auto prev_to = location_idx_t::invalid();
    for (auto const& l : j.legs_) {
      auto const* const ree = std::get_if<journey::run_enter_exit>(&l.uses_);
      if (ree == nullptr) {
        continue;
      }
      if (prev != nullptr &&
          tt.get_complex_idx(prev_to) == tt.get_complex_idx(l.from_) &&
          prev->r_.t_.t_idx_ != transport_idx_t::invalid() &&
          ree->r_.t_.t_idx_ != transport_idx_t::invalid() &&
          tt.transport_route_[prev->r_.t_.t_idx_] ==
              tt.transport_route_[ree->r_.t_.t_idx_]) {
        return true;
      }
      prev = ree;
      prev_to = l.to_;
    }
    return false;
  };

  auto const n_before = journeys_.size();
  utl::erase_if(journeys_, reboards);
  stats_.reboardings_ += n_before - journeys_.size();
}

void bidir_lb_raptor::drop_dominated_continuations() {
  // state after p transports: the vehicles taken so far, each with the stops it
  // was entered and left at. Two journeys sharing it stand at the same station
  // at the same time.
  auto best = std::map<std::vector<std::uint64_t>,
                       std::vector<std::pair<unixtime_t, std::uint8_t>>>{};

  auto const prefixes = [](journey const& j) {
    auto out = std::vector<std::vector<std::uint64_t>>{};
    auto k = std::vector<std::uint64_t>{};
    for (auto const& l : j.legs_) {
      if (auto const* const ree =
              std::get_if<journey::run_enter_exit>(&l.uses_)) {
        k.push_back(to_idx(ree->r_.t_.t_idx_));
        k.push_back(to_idx(ree->r_.t_.day_));
        k.push_back(to_idx(ree->r_.rt_));
        k.push_back(to_idx(l.from_));
        k.push_back(to_idx(l.to_));
        out.push_back(k);
      }
    }
    if (!out.empty()) {
      out.pop_back();  // the full journey is not a prefix of itself
    }
    return out;
  };

  // best first, so a journey is only ever compared against better ones
  utl::sort(journeys_, [](journey const& a, journey const& b) {
    return std::tie(a.dest_time_, a.transfers_) <
           std::tie(b.dest_time_, b.transfers_);
  });

  auto keep = std::vector<journey>{};
  keep.reserve(journeys_.size());
  for (auto& j : journeys_) {
    auto const p = prefixes(j);
    auto dominated = false;
    for (auto const& k : p) {
      auto const it = best.find(k);
      if (it != end(best)) {
        for (auto const& [arr, transfers] : it->second) {
          if (arr <= j.dest_time_ && transfers <= j.transfers_) {
            dominated = true;
            break;
          }
        }
      }
      if (dominated) {
        break;
      }
    }
    if (dominated) {
      ++stats_.dominated_continuations_;
      continue;
    }
    for (auto const& k : p) {
      best[k].emplace_back(j.dest_time_, j.transfers_);
    }
    keep.emplace_back(std::move(j));
  }
  journeys_ = std::move(keep);
}

void bidir_lb_raptor::execute(timetable const& tt,
                              rt_timetable const* rtt,
                              query const& q,
                              bool const arrive_by) {
  reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());
  scored_meetpoints_.clear();

  // init (k = 0)
  init<direction::kForward>(tt, q, arrive_by);
  init<direction::kBackward>(tt, q, arrive_by);

  // run
  //
  // Both directions are advanced every round, even after one of them has run
  // out of stations to relax: `run()` carries `round_times[k-1]` over into
  // `round_times[k]` before it gives up, and the direction that is still going
  // keeps producing meetpoints whose patterns are reconstructed in *both*
  // tables. If the exhausted direction stopped being called, its higher rounds
  // would keep the kUnreachable values from `reset()` and every one of those
  // reconstructions would fail.
  auto any = true;
  for (auto k = 1U;
       any && k != (std::min(q.max_transfers_, kMaxTransfers) + 2U) / 2U; ++k) {
    ++stats_.rounds_;
    auto const t0 = std::chrono::steady_clock::now();
    auto const fwd = run<direction::kForward>(tt, q, k);
    trace_lb("[bidir_lb_raptor][fwd][k={}] meetpoints: {}", k,
             utl::to_vec(meetpoints_,
                         [&](auto const l) { return tt.get_default_name(l); }));
    collect_meetpoints<direction::kForward>(k);
    meetpoints_.clear();

    auto const bwd = run<direction::kBackward>(tt, q, k);
    stats_.round_ms_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
    trace_lb("[bidir_lb_raptor][bwd][k={}] meetpoints: {}", k,
             utl::to_vec(meetpoints_,
                         [&](auto const l) { return tt.get_default_name(l); }));
    collect_meetpoints<direction::kBackward>(k);
    meetpoints_.clear();

    any = fwd || bwd;
  }

  // cheapest order: the dominance filter cuts the set by ~40x first, so the
  // stop-by-stop passthrough scan only runs over what is left
  // All rounds are done, so every meetpoint any of them found is known and
  // comparable - now spend the budget on the globally best ones.
  build_patterns(tt, rtt, q, arrive_by);

  // The result is not bounded by a search window: keep realizing later
  // departures of the patterns that survive until `min_connection_count_`
  // journeys are left, or no pattern has anything more to offer.
  auto const target = std::max(
      std::size_t{1U}, static_cast<std::size_t>(q.min_connection_count_));
  for (auto pass = 0U; pass != kMaxRealizationPasses; ++pass) {
    drop_dominated_continuations();
    drop_destination_passthrough(tt);
    drop_same_route_reboarding(tt);
    drop_terminal_detours(tt);

    // the filters erased and reordered - the index has to follow, and a key
    // whose journey was dropped must not block a better one from a later pass
    journey_index_.clear();
    for (auto const [i, j] : utl::enumerate(journeys_)) {
      journey_index_.emplace(key_of(j), i);
    }

    if (journeys_.size() >= target ||
        !realize_next_departures(tt, rtt, q, arrive_by)) {
      break;
    }
    ++stats_.extra_passes_;
  }

  trace_lb(
      "[bidir_lb_raptor] terminating, pattern_reconstructions: {}, "
      "truncated: {}, repetitions: {}, unrealizable: {}",
      stats_.pattern_reconstructions_, stats_.truncated_patterns_,
      stats_.pattern_repetitions_, stats_.unrealizable_patterns_);
}

routing_result bidir_lb_raptor_search(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    query q,
    direction const search_dir,
    std::optional<std::chrono::seconds> /* timeout */) {
  q.sanitize(tt);

  auto const start = std::chrono::steady_clock::now();

  // `q.start_` is the search source in both directions (motis flips the query
  // for arriveBy), which is the convention `execute` expects.
  auto const anchor = std::visit(
      utl::overloaded{[](interval<unixtime_t> const i) { return i.from_; },
                      [](unixtime_t const t) { return t; }},
      q.start_time_);

  auto lbr = bidir_lb_raptor{};
  lbr.execute(tt, rtt, q, search_dir == direction::kBackward);

  s_state.results_.clear();
  for (auto& j : lbr.journeys_) {
    s_state.results_.add_not_optimal(std::move(j));
  }

  // The window the query came with is not what was searched - the result covers
  // the departures that were realized, so that is what the caller (and its
  // paging cursors) is told.
  auto searched = interval<unixtime_t>{anchor, anchor + duration_t{1}};
  for (auto const& j : s_state.results_) {
    searched.from_ = std::min(searched.from_, j.start_time_);
    searched.to_ = std::max(searched.to_, j.start_time_ + duration_t{1});
  }

  auto const execute_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);

  return routing_result{
      .journeys_ = &s_state.results_,
      .interval_ = searched,
      .search_stats_ = {.execute_time_ = execute_time},
      .algo_stats_ = {
          {"pattern_reconstructions", lbr.stats_.pattern_reconstructions_},
          {"truncated_patterns", lbr.stats_.truncated_patterns_},
          {"pattern_repetitions", lbr.stats_.pattern_repetitions_},
          {"unrealizable_patterns", lbr.stats_.unrealizable_patterns_},
          {"pruned_meetpoints", lbr.stats_.pruned_meetpoints_},
          {"duplicate_journeys", lbr.stats_.duplicate_journeys_},
          {"passthrough_patterns", lbr.stats_.passthrough_patterns_},
          {"dominated_continuations", lbr.stats_.dominated_continuations_},
          {"passthrough_journeys", lbr.stats_.passthrough_journeys_},
          {"reboardings", lbr.stats_.reboardings_},
          {"terminal_detours", lbr.stats_.terminal_detours_},
          {"extra_passes", lbr.stats_.extra_passes_},
          {"rounds", lbr.stats_.rounds_},
          {"round_ms", lbr.stats_.round_ms_},
          {"patterns", lbr.patterns_.size()},
          {"journeys", s_state.results_.size()}}};
}

}  // namespace nigiri::routing