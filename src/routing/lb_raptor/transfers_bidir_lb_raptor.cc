#include "nigiri/routing/lb_raptor/transfers_bidir_lb_raptor.h"

#include <algorithm>
#include <limits>

#include "utl/get_or_create.h"
#include "utl/helpers/algorithm.h"
#include "utl/overloaded.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#define trace_lb(...)
// #define trace_lb fmt::println

namespace nigiri::routing {

// "not riding a transport" while scanning a route. Kept in 32 bit so the
// running travel-time estimate cannot wrap around.
constexpr auto kNoRide = std::numeric_limits<std::uint32_t>::max();

void transfers_bidir_lb_raptor::reset(unsigned const n_locations,
                                      unsigned const n_lb_routes) {
  auto const reset_lb = [&](auto& v) {
    v.resize(n_locations);
    utl::fill(v, kUnreachableLb);
  };
  reset_lb(fwd_lb_);
  reset_lb(bwd_lb_);

  auto const reset_time = [&](auto& v) {
    v.resize(n_locations);
    utl::fill(v, kUnreachableTime);
  };
  reset_time(fwd_time_);
  reset_time(bwd_time_);

  auto const reset_bitvec = [&](auto& v, auto const size) {
    v.resize(size);
    utl::fill(v.blocks_, 0U);
  };

  reset_bitvec(fwd_station_mark_, n_locations);
  reset_bitvec(bwd_station_mark_, n_locations);
  reset_bitvec(prev_station_mark_, n_locations);
  reset_bitvec(is_start_, n_locations);
  reset_bitvec(is_dest_, n_locations);
  reset_bitvec(lb_route_mark_, n_lb_routes);

  journeys_.clear();

  meetpoints_.clear();
  meetpoints_.reserve(1000);

  patterns_.clear();
  stats_.reset();
}

template <direction SearchDir>
void transfers_bidir_lb_raptor::init(timetable const& tt,
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
  auto& lb = kFwd ? fwd_lb_ : bwd_lb_;
  auto& time = kFwd ? fwd_time_ : bwd_time_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;

  min_.clear();
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m = utl::get_or_create(min_, root, [&] { return time[root]; });
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

  for (auto const& [l, t] : min_) {
    for_each_meta(tt, match_mode, l, [&](location_idx_t const meta) {
      time[meta] = std::min(t, time[meta]);
      lb[meta] = 0U;
      station_mark.set(to_idx(meta), true);
      if constexpr (kFwd) {
        is_start_.set(to_idx(meta), true);
      } else {
        is_dest_.set(to_idx(meta), true);
      }
    });
  }
}

template <direction SearchDir>
bool transfers_bidir_lb_raptor::run(timetable const& tt,
                                    query const& q,
                                    unsigned const k) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_root_seq = tt.lb_route_root_seq_[q.prf_idx_];

  auto& lb = kFwd ? fwd_lb_ : bwd_lb_;
  auto const& other_lb = kFwd ? bwd_lb_ : fwd_lb_;
  auto& time = kFwd ? fwd_time_ : bwd_time_;
  auto& station_mark = kFwd ? fwd_station_mark_ : bwd_station_mark_;

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

  auto touched = false;

  // First touch wins: with a transport count as the metric, "reachable with
  // <= k transports" is monotone, so a location that already has a label can
  // never be improved. This is what makes the search a BFS - no location and
  // no route is ever relaxed twice.
  auto const touch = [&](location_idx_t const l, std::uint32_t const t) {
    if (lb[l] != kUnreachableLb) {
      return false;
    }
    lb[l] = static_cast<std::uint8_t>(k);
    time[l] = static_cast<std::uint16_t>(
        std::min(t, static_cast<std::uint32_t>(kUnreachableTime - 1U)));
    station_mark.set(to_idx(l), true);
    touched = true;
    return true;
  };

  auto const expand_fps = [&](location_idx_t const origin,
                              std::uint32_t const t) {
    auto const walk = [&](location_idx_t const y) {
      for (auto const fp : kFwd ? tt.locations_.footpaths_out_[q.prf_idx_][y]
                                : tt.locations_.footpaths_in_[q.prf_idx_][y]) {
        touch(tt.locations_.get_root_idx(fp.target()),
              t + static_cast<std::uint32_t>(adjusted_transfer_time(
                      q.transfer_time_settings_, fp.duration().count())));
      }
    };

    walk(origin);
    for (auto const c : tt.locations_.children_[origin]) {
      walk(c);
      for (auto const cc : tt.locations_.children_[c]) {
        walk(cc);
      }
    }
  };

  // Arrival at `l` by transport; `t` is the pre-transfer arrival estimate.
  auto const relax = [&](location_idx_t const l, std::uint32_t const t) {
    if (touch(l, t + static_cast<std::uint32_t>(adjusted_transfer_time(
                         q.transfer_time_settings_,
                         tt.locations_.transfer_time_[l].count())))) {
      expand_fps(l, t);
    }
  };

  // One pass per route instead of one pass per (route, boarding stop): the
  // running estimate `t` simply joins whatever the best boarding stop seen so
  // far offers. `bidir_lb_raptor` cannot do this because it has to relax
  // against per-round labels; here every stop is written at most once.
  lb_route_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const r = lb_route_idx_t{i};
    auto const segment_layovers = route_times[r];
    auto const seq = route_root_seq[r];
    auto const n = static_cast<unsigned>(seq.size());

    auto t = kNoRide;
    for (auto x = 0U; x != n; ++x) {
      auto const pos = kFwd ? x : n - x - 1U;
      auto const l = seq[pos];

      if (t != kNoRide) {
        relax(l, t);
        // Riding through an interior stop costs its layover; boarding below
        // does not, which is why the boarding join comes afterwards.
        if (pos != 0U && pos + 1U != n) {
          t += static_cast<std::uint32_t>(
              segment_layovers[pos * 2U - 1U].count());
        }
      }

      if (prev_station_mark_.test(to_idx(l)) && time[l] < t) {
        t = time[l];
      }

      if (t != kNoRide && x + 1U != n) {
        t += static_cast<std::uint32_t>(
            kFwd ? segment_layovers[pos * 2U].count()
                 : segment_layovers[(pos - 1U) * 2U].count());
      }
    }
  });

  utl::fill(lb_route_mark_.blocks_, 0U);

  if (!touched) {
    return false;
  }

  // A location the other direction has already labelled is a meetpoint. Like
  // `bidir_lb_raptor` it is taken out of the frontier: everything behind it is
  // covered by the other search.
  station_mark.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};
    if (other_lb[l] == kUnreachableLb) {
      return;
    }
    station_mark.set(i, false);
    if (utl::find_if(meetpoints_, [&](auto const m) {
          return matches(tt, location_match_mode::kEquivalent, l, m);
        }) == end(meetpoints_)) {
      meetpoints_.push_back(l);
    }
  });

  return station_mark.any();
}

void transfers_bidir_lb_raptor::execute(timetable const& tt,
                                        rt_timetable const* rtt,
                                        query const& q,
                                        bool const arrive_by) {
  reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());

  // init (k = 0)
  init<direction::kForward>(tt, q, arrive_by);
  init<direction::kBackward>(tt, q, arrive_by);

  // Unlike `bidir_lb_raptor` there is no need to keep advancing an exhausted
  // direction: `lb_` holds absolute labels, so a chain enumerated later still
  // finds every predecessor it needs.
  auto any = true;
  for (auto k = 1U;
       any && k != (std::min(q.max_transfers_, kMaxTransfers) + 2U) / 2U; ++k) {
    auto const fwd = run<direction::kForward>(tt, q, k);
    trace_lb("[transfers_bidir][fwd][k={}] {} meetpoints", k,
             meetpoints_.size());
    meetpoints_to_patterns(tt, rtt, q, arrive_by);
    meetpoints_.clear();

    auto const bwd = run<direction::kBackward>(tt, q, k);
    trace_lb("[transfers_bidir][bwd][k={}] {} meetpoints", k,
             meetpoints_.size());
    meetpoints_to_patterns(tt, rtt, q, arrive_by);
    meetpoints_.clear();

    any = fwd || bwd;
  }
}

routing_result transfers_bidir_lb_raptor_search(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    query q,
    direction const search_dir,
    std::optional<std::chrono::seconds> /* timeout */) {
  q.sanitize(tt);

  auto const start = std::chrono::steady_clock::now();

  auto const search_interval = std::visit(
      utl::overloaded{[](interval<unixtime_t> const i) { return i; },
                      [](unixtime_t const t) {
                        return interval<unixtime_t>{t, t + duration_t{1}};
                      }},
      q.start_time_);

  auto lbr = transfers_bidir_lb_raptor{};
  lbr.execute(tt, rtt, q, search_dir == direction::kBackward);

  s_state.results_.clear();
  for (auto& j : lbr.journeys_) {
    s_state.results_.add_not_optimal(std::move(j));
  }

  auto const execute_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);

  return routing_result{
      .journeys_ = &s_state.results_,
      .interval_ = search_interval,
      .search_stats_ = {.execute_time_ = execute_time},
      .algo_stats_ = {
          {"pattern_reconstructions", lbr.stats_.pattern_reconstructions_},
          {"truncated_patterns", lbr.stats_.truncated_patterns_},
          {"pattern_repetitions", lbr.stats_.pattern_repetitions_},
          {"unrealizable_patterns", lbr.stats_.unrealizable_patterns_},
          {"pruned_meetpoints", lbr.stats_.pruned_meetpoints_},
          {"chain_enumerations", lbr.stats_.chain_enumerations_},
          {"pred_expansions", lbr.stats_.pred_expansions_},
          {"dead_ends", lbr.stats_.dead_ends_},
          {"patterns", lbr.patterns_.size()},
          {"journeys", s_state.results_.size()}}};
}

}  // namespace nigiri::routing
