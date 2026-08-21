#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include <algorithm>
#include <variant>

#include "utl/enumerate.h"
#include "utl/to_vec.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/lb_raptor/pattern_to_journey.h"
#include "nigiri/timetable.h"

// #define trace(...)
#define trace fmt::println

namespace nigiri::routing {
constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

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
    trace("[find_in_prev_round][{}] location={}, round={}, time={}",
          kFwd ? "fwd" : "bwd", tt.get_default_name(x), k, time);

    for (auto const r : tt.location_lb_routes_[q.prf_idx_][x]) {
      auto const& seq = tt.lb_route_root_seq_[q.prf_idx_][r];

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
    trace("[reconstruct][{}][k={}] {} not reached, nothing to reconstruct",
          kFwd ? "fwd" : "bwd", k_start, tt.get_default_name(l));
    return false;
  }

  auto cur = l;
  for (auto k = k_start; k != 0U; --k) {
    if (is_terminal.test(to_idx(cur))) {
      trace("[reconstruct][{}][k={}] reached terminal {}, terminating",
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
              tt.locations_.get_root_idx(fp.target()), k,
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
      trace(
          "[reconstruct][{}][k={}][cur={}] failed, could not find matching "
          "entry in previous round,",
          kFwd ? "fwd" : "bwd", k, tt.get_default_name(cur));
      return false;
    }

    trace("[reconstruct][{}][k={}][cur={}] adding {} to pattern",
          kFwd ? "fwd" : "bwd", k, tt.get_default_name(cur),
          tt.get_default_name(*prev));
    current_pattern_.emplace_back(*prev);
    cur = *prev;
  }

  return is_terminal.test(to_idx(cur));
}

template <direction SearchDir>
void bidir_lb_raptor::meetpoints_to_patterns(timetable const& tt,
                                             rt_timetable const* rtt,
                                             query const& q,
                                             unsigned const k,
                                             bool const arrive_by) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const to_array = [&](auto const& v) {
    auto a = std::array<location_idx_t, kMaxTransfers + 2U>{};
    utl::fill(a, location_idx_t::invalid());
    for (auto const [i, e] : utl::enumerate(v)) {
      a[i] = e;
    }
    return a;
  };

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

  scored_meetpoints_.clear();
  for (auto const m : meetpoints_) {
    // A meetpoint may be the start or the destination itself - that is how
    // patterns without any transfer (a single transport from start to
    // destination) are found. `reconstruct` returns immediately when it is
    // handed a terminal, so the corresponding half of the pattern stays empty
    // and the other half is built as usual. Only a meetpoint that is start
    // *and* destination is degenerate.
    if (is_start_.test(to_idx(m)) && is_dest_.test(to_idx(m))) {
      trace("[meetpoints_to_patterns][{}][k={}] skipping start == destination",
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
    scored_meetpoints_.emplace_back(scored_meetpoint{
        .l_ = m,
        .transfers_ = static_cast<std::uint8_t>(
            n_transports == 0U ? 0U : n_transports - 1U),
        .travel_time_lb_ =
            static_cast<std::uint32_t>(fwd_t) + static_cast<std::uint32_t>(bwd_t)});
  }

  // Rank by (transfers, travel time) and keep only the best ones - everything
  // below the cut is never reconstructed.
  if (scored_meetpoints_.size() > max_patterns_per_round_) {
    std::partial_sort(begin(scored_meetpoints_),
                      begin(scored_meetpoints_) + max_patterns_per_round_,
                      end(scored_meetpoints_));
    stats_.pruned_meetpoints_ +=
        scored_meetpoints_.size() - max_patterns_per_round_;
    scored_meetpoints_.resize(max_patterns_per_round_);
  } else {
    std::sort(begin(scored_meetpoints_), end(scored_meetpoints_));
  }

  for (auto const& [m, transfers, travel_time_lb] : scored_meetpoints_) {
    trace("[meetpoints_to_patterns][{}][k={}] meetpoint: {} ({} transfers, {})",
          kFwd ? "fwd" : "bwd", k, tt.get_default_name(m), transfers,
          travel_time_lb);

    ++stats_.pattern_reconstructions_;
    current_pattern_.clear();
    auto complete = reconstruct<direction::kForward>(tt, q, m, k);
    std::ranges::reverse(current_pattern_);
    current_pattern_.emplace_back(m);
    complete =
        reconstruct<direction::kBackward>(tt, q, m, bwd_k_start) && complete;

    if (!complete) {
      ++stats_.truncated_patterns_;
      trace("[meetpoints_to_patterns][{}][k={}] truncated pattern: {}",
            kFwd ? "fwd" : "bwd", k,
            utl::to_vec(current_pattern_,
                        [&](auto const l) { return tt.get_default_name(l); }));
      continue;
    }

    // Patterns repeat across meetpoints and rounds; realizing one is by far
    // the most expensive step, so skip it for a pattern already seen.
    if (!patterns_.emplace(to_array(current_pattern_)).second) {
      ++stats_.pattern_repetitions_;
      trace("[meetpoints_to_patterns][{}][k={}] pattern repetition: {}",
            kFwd ? "fwd" : "bwd", k,
            utl::to_vec(current_pattern_,
                        [&](auto const l) { return tt.get_default_name(l); }));
      continue;
    }

    trace("[meetpoints_to_patterns][{}][k={}] new pattern: {}",
          kFwd ? "fwd" : "bwd", k,
          utl::to_vec(current_pattern_,
                      [&](auto const l) { return tt.get_default_name(l); }));

    // An interval query asks for every departure in the interval, an ontrip
    // query for the single earliest/latest one.
    auto const n_before = journeys_.size();
    if (auto const* const iv =
            std::get_if<interval<unixtime_t>>(&q.start_time_);
        iv != nullptr) {
      if (arrive_by) {
        pattern_to_journeys<direction::kBackward>(
            tt, rtt, q, current_pattern_, *iv, is_src_, is_dst_, journeys_);
      } else {
        pattern_to_journeys<direction::kForward>(
            tt, rtt, q, current_pattern_, *iv, is_src_, is_dst_, journeys_);
      }
    } else {
      auto j = arrive_by ? pattern_to_journey<direction::kBackward>(
                               tt, rtt, q, current_pattern_, is_src_, is_dst_)
                         : pattern_to_journey<direction::kForward>(
                               tt, rtt, q, current_pattern_, is_src_, is_dst_);
      if (j.has_value()) {
        journeys_.emplace_back(std::move(*j));
      }
    }

    if (journeys_.size() == n_before) {
      ++stats_.unrealizable_patterns_;
      trace("[meetpoints_to_patterns][{}][k={}] pattern not realizable",
            kFwd ? "fwd" : "bwd", k);
    } else {
      trace("[meetpoints_to_patterns][{}][k={}] {} journeys", kFwd ? "fwd" : "bwd",
            k, journeys_.size() - n_before);
    }
  }
}

template bool bidir_lb_raptor::reconstruct<direction::kForward>(
    timetable const&, query const&, location_idx_t, unsigned);
template bool bidir_lb_raptor::reconstruct<direction::kBackward>(
    timetable const&, query const&, location_idx_t, unsigned);

template void bidir_lb_raptor::meetpoints_to_patterns<direction::kForward>(
    timetable const&, rt_timetable const*, query const&, unsigned, bool);
template void bidir_lb_raptor::meetpoints_to_patterns<direction::kBackward>(
    timetable const&, rt_timetable const*, query const&, unsigned, bool);

}  // namespace nigiri::routing