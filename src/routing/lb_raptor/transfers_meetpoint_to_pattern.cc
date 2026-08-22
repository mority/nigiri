#include "nigiri/routing/lb_raptor/transfers_bidir_lb_raptor.h"

#include <algorithm>
#include <variant>

#include "utl/enumerate.h"
#include "utl/erase_duplicates.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/lb_raptor/pattern_to_journey.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

#define trace_lb(...)
// #define trace_lb fmt::println

namespace nigiri::routing {

template <direction SearchDir>
void transfers_bidir_lb_raptor::find_preds(timetable const& tt,
                                           query const& q,
                                           location_idx_t const cur,
                                           std::vector<location_idx_t>& out) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  static constexpr auto kStep = kFwd ? -1 : 1;

  auto const& lb = kFwd ? fwd_lb_ : bwd_lb_;
  auto const& time = kFwd ? fwd_time_ : bwd_time_;

  ++stats_.pred_expansions_;
  out.clear();

  auto const target_lb = static_cast<std::uint8_t>(lb[cur] - 1U);

  // `y` is where the transport is left. Every stop it serves before (kFwd)
  // resp. after (kBwd) `y` whose label is one transport smaller is a valid
  // boarding stop - the route ride relaxed the whole suffix in `run()`, so
  // there is nothing else to check. This is exactly where the single parent
  // pointer of `bidir_lb_raptor::find_in_prev_round` turns into a set.
  auto const scan_from = [&](location_idx_t const y) {
    for (auto const r : tt.location_lb_routes_[q.prf_idx_][y]) {
      auto const seq = tt.lb_route_root_seq_[q.prf_idx_][r];
      auto const n = static_cast<std::int32_t>(seq.size());
      for (auto i = 0; i != n; ++i) {
        if (seq[static_cast<unsigned>(i)] != y) {
          continue;
        }
        for (auto o = i + kStep; 0 <= o && o < n; o += kStep) {
          auto const p = seq[static_cast<unsigned>(o)];
          if (lb[p] == target_lb) {
            out.push_back(p);
          }
        }
      }
    }
  };

  // The transport may also have been left one footpath away from `cur`.
  auto const fp_scan = [&](location_idx_t const y) {
    for (auto const fp : kFwd ? tt.locations_.footpaths_in_[q.prf_idx_][y]
                              : tt.locations_.footpaths_out_[q.prf_idx_][y]) {
      scan_from(tt.locations_.get_root_idx(fp.target()));
    }
  };

  scan_from(cur);
  fp_scan(cur);
  for (auto const c : tt.locations_.children_[cur]) {
    fp_scan(c);
    for (auto const cc : tt.locations_.children_[c]) {
      fp_scan(cc);
    }
  }

  utl::erase_duplicates(out);

  // Without the travel-time estimate every candidate is equally good and the
  // pattern may ride to the far end of a line for no reason. This is the one
  // piece of information the transfer count cannot supply itself.
  if (order_preds_by_time_) {
    std::sort(begin(out), end(out),
              [&](auto const a, auto const b) { return time[a] < time[b]; });
  }
  if (out.size() > max_preds_per_step_) {
    out.resize(max_preds_per_step_);
  }
}

template <direction SearchDir>
void transfers_bidir_lb_raptor::enumerate_chains(timetable const& tt,
                                                 query const& q,
                                                 location_idx_t const m) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const& lb = kFwd ? fwd_lb_ : bwd_lb_;
  auto const& is_terminal = kFwd ? is_start_ : is_dest_;
  auto& out = kFwd ? fwd_chains_ : bwd_chains_;

  out.clear();
  chain_stack_.clear();

  if (lb[m] == kUnreachableLb) {
    return;
  }

  auto const rec = [&](auto&& self, location_idx_t const cur,
                       unsigned const depth) -> void {
    if (out.size() >= max_chains_per_meetpoint_) {
      return;
    }
    // A meetpoint that is the terminal itself yields the empty chain, which is
    // how patterns without a transfer on this side are produced.
    if (is_terminal.test(to_idx(cur))) {
      auto& c = out.emplace_back();
      c.n_ = static_cast<std::uint8_t>(chain_stack_.size());
      for (auto i = 0U; i != chain_stack_.size(); ++i) {
        c.l_[i] = chain_stack_[i];
      }
      ++stats_.chain_enumerations_;
      return;
    }
    if (lb[cur] == 0U || lb[cur] == kUnreachableLb ||
        depth >= pred_scratch_.size()) {
      return;
    }

    auto& cand = pred_scratch_[depth];
    find_preds<SearchDir>(tt, q, cur, cand);
    if (cand.empty()) {
      ++stats_.dead_ends_;
      return;
    }

    for (auto const p : cand) {
      chain_stack_.push_back(p);
      self(self, p, depth + 1U);
      chain_stack_.pop_back();
      if (out.size() >= max_chains_per_meetpoint_) {
        break;
      }
    }
  };

  rec(rec, m, 0U);
}

void transfers_bidir_lb_raptor::meetpoints_to_patterns(timetable const& tt,
                                                       rt_timetable const* rtt,
                                                       query const& q,
                                                       bool const arrive_by) {
  auto const to_array = [&](auto const& v) {
    auto a = std::array<location_idx_t, kMaxTransfers + 2U>{};
    utl::fill(a, location_idx_t::invalid());
    for (auto const [i, e] : utl::enumerate(v)) {
      a[i] = e;
    }
    return a;
  };

  scored_meetpoints_.clear();
  for (auto const m : meetpoints_) {
    // Only a meetpoint that is start *and* destination is degenerate; being
    // just one of the two is how zero-transfer patterns are found.
    if (is_start_.test(to_idx(m)) && is_dest_.test(to_idx(m))) {
      continue;
    }

    auto const f = fwd_lb_[m];
    auto const b = bwd_lb_[m];
    if (f == kUnreachableLb || b == kUnreachableLb) {
      continue;
    }

    // Exact in the relaxed model - no `effective_round()` walk needed, the
    // label *is* the round. The transfer at the meetpoint is counted by both
    // labels, a constant offset that does not affect the ranking.
    auto const n_transports =
        static_cast<unsigned>(f) + static_cast<unsigned>(b);
    scored_meetpoints_.emplace_back(scored_meetpoint{
        .l_ = m,
        .transfers_ = static_cast<std::uint8_t>(
            n_transports == 0U ? 0U : n_transports - 1U),
        .travel_time_lb_ =
            order_preds_by_time_
                ? static_cast<std::uint32_t>(fwd_time_[m]) +
                      static_cast<std::uint32_t>(bwd_time_[m])
                : 0U});
  }

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

  for (auto const& sm : scored_meetpoints_) {
    auto const m = sm.l_;

    ++stats_.pattern_reconstructions_;
    enumerate_chains<direction::kForward>(tt, q, m);
    enumerate_chains<direction::kBackward>(tt, q, m);

    if (fwd_chains_.empty() || bwd_chains_.empty()) {
      ++stats_.truncated_patterns_;
      trace_lb("[transfers_meetpoint] {} truncated (fwd={}, bwd={})",
               tt.get_default_name(m), fwd_chains_.size(), bwd_chains_.size());
      continue;
    }

    auto produced = 0U;
    for (auto const& f : fwd_chains_) {
      if (produced >= max_patterns_per_meetpoint_) {
        break;
      }
      for (auto const& b : bwd_chains_) {
        if (produced >= max_patterns_per_meetpoint_) {
          break;
        }
        ++produced;

        // `f` runs from the meetpoint back to the start, so it goes in
        // reversed; `b` already runs towards the destination.
        current_pattern_.clear();
        for (auto i = f.n_; i != 0U; --i) {
          current_pattern_.emplace_back(f.l_[i - 1U]);
        }
        current_pattern_.emplace_back(m);
        for (auto i = 0U; i != b.n_; ++i) {
          current_pattern_.emplace_back(b.l_[i]);
        }

        if (current_pattern_.size() > kMaxTransfers + 2U) {
          continue;
        }

        if (!patterns_.emplace(to_array(current_pattern_)).second) {
          ++stats_.pattern_repetitions_;
          continue;
        }

        trace_lb("[transfers_meetpoint] new pattern: {}",
                 utl::to_vec(current_pattern_, [&](auto const l) {
                   return tt.get_default_name(l);
                 }));

        auto const n_before = journeys_.size();
        if (auto const* const iv =
                std::get_if<interval<unixtime_t>>(&q.start_time_);
            iv != nullptr) {
          if (arrive_by) {
            pattern_to_journeys<direction::kBackward>(tt, rtt, q,
                                                      current_pattern_, *iv,
                                                      journeys_);
          } else {
            pattern_to_journeys<direction::kForward>(tt, rtt, q,
                                                     current_pattern_, *iv,
                                                     journeys_);
          }
        } else {
          auto j = arrive_by ? pattern_to_journey<direction::kBackward>(
                                   tt, rtt, q, current_pattern_)
                             : pattern_to_journey<direction::kForward>(
                                   tt, rtt, q, current_pattern_);
          if (j.has_value()) {
            journeys_.emplace_back(std::move(*j));
          }
        }

        if (journeys_.size() == n_before) {
          ++stats_.unrealizable_patterns_;
        }
      }
    }
  }
}

}  // namespace nigiri::routing
