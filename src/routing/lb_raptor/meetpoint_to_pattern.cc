#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include <algorithm>
#include <span>
#include <variant>

#include "utl/enumerate.h"
#include "utl/to_vec.h"

#include "nigiri/routing/journey.h"
#include "utl/overloaded.h"

#include "nigiri/routing/lb_raptor/pattern_to_journey.h"
#include "nigiri/timetable.h"

#define trace_lb(...)
// #define trace_lb fmt::println

namespace nigiri::routing {
constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

namespace {

// The transit core: per transport, the vehicle and the stops it is entered and
// left at. Two journeys agreeing on it are the same connection *including where
// the passenger changes*; they can then only differ in the access and egress
// walks, and their times only by the length of those, so the tightest one is
// kept.
//
// The transfer stops are deliberately part of the key: changing between the
// same two vehicles at a different station is a transfer pattern of its own,
// and enumerating those is what this algorithm is for.
}  // namespace

bidir_lb_raptor::journey_key bidir_lb_raptor::key_of(journey const& j) {
  auto k = bidir_lb_raptor::journey_key{};
  k.reserve(1U + 5U * j.legs_.size());
  k.push_back(j.transfers_);
  for (auto const& l : j.legs_) {
    if (auto const* const ree =
            std::get_if<journey::run_enter_exit>(&l.uses_)) {
      k.push_back(to_idx(ree->r_.t_.t_idx_));
      k.push_back(to_idx(ree->r_.t_.day_));
      k.push_back(to_idx(ree->r_.rt_));
      k.push_back(to_idx(l.from_));
      k.push_back(to_idx(l.to_));
    }
  }
  return k;
}

namespace {

// where the first realization of every pattern sets off from: the query's own
// anchor. The search window plays no part any more - the number of journeys
// asked for does.
unixtime_t anchor(query const& q) {
  return std::visit(
      utl::overloaded{[](unixtime_t const t) { return t; },
                      [](interval<unixtime_t> const i) { return i.from_; }},
      q.start_time_);
}

}  // namespace

// door to door, in travel order
duration_t bidir_lb_raptor::span(journey const& j) {
  return j.legs_.empty()
             ? duration_t{0}
             : duration_t{j.legs_.back().arr_time_ - j.legs_.front().dep_time_};
}

// everything that is not a transport: access, transfer and egress walks
duration_t bidir_lb_raptor::walk_time(journey const& j) {
  auto d = duration_t{0};
  for (auto const& l : j.legs_) {
    if (!std::holds_alternative<journey::run_enter_exit>(l.uses_)) {
      d += (l.arr_time_ - l.dep_time_);
    }
  }
  return d;
}

template <direction SearchDir>
bool bidir_lb_raptor::reconstruct(timetable const& tt,
                                  query const& q,
                                  location_idx_t const l,
                                  unsigned const k_start) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto const& is_terminal = kFwd ? is_start_ : is_dest_;

  auto const find_in_prev_round =
      [&](location_idx_t const x, unsigned const k,
          std::uint16_t const time) -> std::optional<location_idx_t> {
    trace_lb("[find_in_prev_round][{}] location={}, round={}, time={}",
             kFwd ? "fwd" : "bwd", tt.get_default_name(x), k, time);

    for (auto const r : tt.location_lb_routes_[q.prf_idx_][x]) {
      auto const& seq = tt.lb_route_complex_seq_[q.prf_idx_][r];

      for (auto i = 0U; i != seq.size(); ++i) {
        auto const in = kFwd ? seq.size() - i - 1U : i;
        auto const l_in = seq[in];
        if (l_in != x) {
          continue;
        }

        auto t = time;
        static constexpr auto step = kFwd ? -1 : 1;
        for (auto out = static_cast<std::int32_t>(in + step);
             0 <= out && out < static_cast<std::int32_t>(seq.size());
             out += step) {
          auto const l_out = seq[out];
          auto const segment =
              kFwd ? tt.get_departing_segment_lb(q.prf_idx_, r, out)
                   : tt.get_arriving_segment_lb(q.prf_idx_, r, out);

          t -= segment.count();
          // `t` is unsigned and may have wrapped around; an unreachable label
          // must never be accepted as a predecessor.
          if (t == round_times[k - 1U][l_out] && t != kUnreachable) {
            return l_out;
          }
          if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
            t -= tt.get_layover_lb(q.prf_idx_, r, out).count();
          }
        }
      }
    }

    return std::nullopt;
  };

  if (round_times[k_start][l] == kUnreachable) {
    trace_lb("[reconstruct][{}][k={}] {} not reached, nothing to reconstruct",
             kFwd ? "fwd" : "bwd", k_start, tt.get_default_name(l));
    return false;
  }

  auto cur = l;
  for (auto k = k_start; k != 0U; --k) {
    if (is_terminal.test(to_idx(cur))) {
      trace_lb("[reconstruct][{}][k={}] reached terminal {}, terminating",
               kFwd ? "fwd" : "bwd", k, tt.get_default_name(cur));
      return true;
    }

    // `run()` copies round k-1 into round k before relaxing, so the label at
    // `cur` may just have been carried forward. Walk back to the round that
    // actually created it - only there does a predecessor exist in k-1.
    while (k > 1U && round_times[k][cur] == round_times[k - 1U][cur]) {
      --k;
    }

    auto const time = round_times[k][cur];

    auto const local_transfer =
        [&](location_idx_t const x) -> std::optional<location_idx_t> {
      return find_in_prev_round(
          x, k,
          time -
              adjusted_transfer_time(q.transfer_time_settings_,
                                     tt.locations_.transfer_time_[x].count()));
    };

    auto const footpath_transfer =
        [&](location_idx_t const x) -> std::optional<location_idx_t> {
      auto const expand_fps = [&](auto const y) {
        auto found = std::optional<location_idx_t>{};
        for (auto const fp :
             kFwd ? tt.locations_.footpaths_in_[q.prf_idx_][y]
                  : tt.locations_.footpaths_out_[q.prf_idx_][y]) {
          found = find_in_prev_round(
              tt.get_complex_idx(fp.target()), k,
              time - adjusted_transfer_time(q.transfer_time_settings_,
                                            fp.duration())
                         .count());
          if (found) {
            break;
          }
        }
        return found;
      };

      auto found = expand_fps(x);
      if (found) {
        return found;
      }
      for (auto const c : tt.locations_.children_[x]) {
        found = expand_fps(c);
        if (found) {
          return found;
        }
        for (auto const cc : tt.locations_.children_[c]) {
          found = expand_fps(cc);
          if (found) {
            return found;
          }
        }
      }

      return std::nullopt;
    };

    auto prev = local_transfer(cur);
    if (!prev) {
      prev = footpath_transfer(cur);
    }
    if (!prev) {
      trace_lb(
          "[reconstruct][{}][k={}][cur={}] failed, could not find matching "
          "entry in previous round,",
          kFwd ? "fwd" : "bwd", k, tt.get_default_name(cur));
      return false;
    }

    trace_lb("[reconstruct][{}][k={}][cur={}] adding {} to pattern",
             kFwd ? "fwd" : "bwd", k, tt.get_default_name(cur),
             tt.get_default_name(*prev));
    current_pattern_.emplace_back(*prev);
    cur = *prev;
  }

  return is_terminal.test(to_idx(cur));
}

template <direction SearchDir>
void bidir_lb_raptor::collect_meetpoints(unsigned const k) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  // Round in which the label at `x` was created: `run()` copies round k-1 into
  // round k, so a carried-forward label costs fewer transports than `k_start`
  // suggests. This is the same round `reconstruct` will start its walk from.
  auto const effective_round = [](auto const& round_times,
                                  location_idx_t const x, unsigned k_start) {
    while (k_start != 0U &&
           round_times[k_start][x] == round_times[k_start - 1U][x]) {
      --k_start;
    }
    return k_start;
  };

  auto const bwd_k_start = kFwd ? k - 1U : k;

  for (auto const m : meetpoints_) {
    // A meetpoint may be the start or the destination itself - that is how
    // patterns without any transfer (a single transport from start to
    // destination) are found. `reconstruct` returns immediately when it is
    // handed a terminal, so the corresponding half of the pattern stays empty
    // and the other half is built as usual. Only a meetpoint that is start
    // *and* destination is degenerate.
    if (is_start_.test(to_idx(m)) && is_dest_.test(to_idx(m))) {
      trace_lb("[collect_meetpoints][{}][k={}] skipping start == destination",
               kFwd ? "fwd" : "bwd", k);
      continue;
    }

    auto const fwd_t = fwd_round_times_[k][m];
    auto const bwd_t = bwd_round_times_[bwd_k_start][m];
    if (fwd_t == kUnreachable || bwd_t == kUnreachable) {
      continue;
    }

    // One transport per round on either side; the transfer at the meetpoint is
    // counted by both labels, which is a constant offset and does not affect
    // the ranking.
    auto const n_transports = effective_round(fwd_round_times_, m, k) +
                              effective_round(bwd_round_times_, m, bwd_k_start);
    scored_meetpoints_.emplace_back(
        scored_meetpoint{.l_ = m,
                         .transfers_ = static_cast<std::uint8_t>(
                             n_transports == 0U ? 0U : n_transports - 1U),
                         .travel_time_lb_ = static_cast<std::uint32_t>(fwd_t) +
                                            static_cast<std::uint32_t>(bwd_t),
                         .fwd_k_ = k,
                         .bwd_k_ = bwd_k_start});
  }
}

void bidir_lb_raptor::build_patterns(timetable const& tt,
                                     rt_timetable const* rtt,
                                     query const& q,
                                     bool const arrive_by) {
  auto const to_array = [&](auto const& v) {
    auto a = bidir_lb_raptor::pattern_t{};
    utl::fill(a, location_idx_t::invalid());
    for (auto const [i, e] : utl::enumerate(v)) {
      a[i] = e;
    }
    return a;
  };

  // Two pointless detours a reconstructed pattern can contain, both cut here:
  //
  //  - it comes back to a station it already visited. Staying there instead
  //    cannot arrive later - the onward transport is boardable from that same
  //    station, and it is reached earlier.
  //  - it runs *through* a terminal. The passenger could have got off there
  //    (or set off from there), which again arrives earlier with fewer
  //    transfers.
  //
  // Cutting is always safe: what remains is the same journey with a piece
  // dropped, so it is realizable by construction. `patterns_` then absorbs the
  // result if the cut pattern was already seen.
  auto const simplify_pattern = [&]() {
    auto cut = false;

    for (auto i = std::size_t{0U}; i + 1U < current_pattern_.size(); ++i) {
      auto const same =
          std::find(std::next(begin(current_pattern_),
                              static_cast<std::ptrdiff_t>(i + 1U)),
                    end(current_pattern_), current_pattern_[i]);
      if (same != end(current_pattern_)) {
        current_pattern_.erase(std::next(begin(current_pattern_),
                                         static_cast<std::ptrdiff_t>(i + 1U)),
                               std::next(same));
        cut = true;
      }
    }

    auto const n = current_pattern_.size();
    auto first = std::size_t{0U}, last = n - 1U;
    for (auto i = std::size_t{1U}; i + 1U != n; ++i) {
      auto const l = to_idx(current_pattern_[i]);
      if (is_start_.test(l)) {
        first = i;
      }
      if (is_dest_.test(l) && last == n - 1U) {
        last = i;
      }
    }
    if (first != 0U || last != n - 1U) {
      current_pattern_ = std::vector<location_idx_t>{
          std::next(begin(current_pattern_),
                    static_cast<std::ptrdiff_t>(first)),
          std::next(begin(current_pattern_),
                    static_cast<std::ptrdiff_t>(last) + 1)};
      cut = true;
    }

    if (cut) {
      ++stats_.passthrough_patterns_;
    }
  };

  // One budget for the whole query: rank every meetpoint any round found by
  // (transfers, travel time) and reconstruct the best ones. Everything below
  // the cut is never reconstructed.
  if (scored_meetpoints_.size() > max_patterns_) {
    std::partial_sort(begin(scored_meetpoints_),
                      begin(scored_meetpoints_) + max_patterns_,
                      end(scored_meetpoints_));
    stats_.pruned_meetpoints_ += scored_meetpoints_.size() - max_patterns_;
    scored_meetpoints_.resize(max_patterns_);
  } else {
    std::sort(begin(scored_meetpoints_), end(scored_meetpoints_));
  }

  for (auto const& sm : scored_meetpoints_) {
    auto const m = sm.l_;
    trace_lb("[build_patterns] meetpoint: {} ({} transfers, {})",
             tt.get_default_name(m), sm.transfers_, sm.travel_time_lb_);

    ++stats_.pattern_reconstructions_;
    current_pattern_.clear();
    auto complete = reconstruct<direction::kForward>(tt, q, m, sm.fwd_k_);
    std::ranges::reverse(current_pattern_);
    current_pattern_.emplace_back(m);
    complete =
        reconstruct<direction::kBackward>(tt, q, m, sm.bwd_k_) && complete;

    if (!complete) {
      ++stats_.truncated_patterns_;
      trace_lb("[build_patterns] truncated pattern: {}",
               utl::to_vec(current_pattern_, [&](auto const l) {
                 return tt.get_default_name(l);
               }));
      continue;
    }

    // Patterns repeat across meetpoints and rounds; realizing one is by far
    // the most expensive step, so skip it for a pattern already seen.
    simplify_pattern();

    if (!patterns_.emplace(to_array(current_pattern_)).second) {
      ++stats_.pattern_repetitions_;
      trace_lb("[build_patterns] pattern repetition: {}",
               utl::to_vec(current_pattern_, [&](auto const l) {
                 return tt.get_default_name(l);
               }));
      continue;
    }

    trace_lb("[build_patterns] new pattern: {}",
             utl::to_vec(current_pattern_,
                         [&](auto const l) { return tt.get_default_name(l); }));

    // One journey per pattern for now - the earliest (kFwd) / latest (kBwd)
    // one at the query's anchor. `execute` realizes the next departures
    // afterwards, as often as `min_connection_count_` needs.
    new_journeys_.clear();
    {
      auto j = arrive_by ? pattern_to_journey_at<direction::kBackward>(
                               tt, rtt, q, current_pattern_, anchor(q))
                         : pattern_to_journey_at<direction::kForward>(
                               tt, rtt, q, current_pattern_, anchor(q));
      if (j.has_value()) {
        new_journeys_.emplace_back(std::move(*j));
      }
    }

    if (new_journeys_.empty()) {
      ++stats_.unrealizable_patterns_;
      trace_lb("[build_patterns] pattern not realizable");
    } else {
      trace_lb("[build_patterns] {} journeys", new_journeys_.size());
    }

    if (!new_journeys_.empty()) {
      auto const step = arrive_by ? duration_t{-1} : duration_t{1};
      realizable_.push_back(
          realizable{.pattern_ = current_pattern_,
                     .next_anchor_ = new_journeys_.front().start_time_ + step});
    }

    for (auto& j : new_journeys_) {
      pattern_of_.emplace(key_of(j), realizable_.size() - 1U);
      add_journey(std::move(j));
    }
  }
}

template bool bidir_lb_raptor::reconstruct<direction::kForward>(
    timetable const&, query const&, location_idx_t, unsigned);
template bool bidir_lb_raptor::reconstruct<direction::kBackward>(
    timetable const&, query const&, location_idx_t, unsigned);

template void bidir_lb_raptor::collect_meetpoints<direction::kForward>(
    unsigned);
template void bidir_lb_raptor::collect_meetpoints<direction::kBackward>(
    unsigned);

}  // namespace nigiri::routing