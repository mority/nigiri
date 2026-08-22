// A/B comparison of `bidir_lb_raptor` (travel-time lower bounds, one pattern
// per meetpoint) against `transfers_bidir_lb_raptor` (transport-count labels,
// bounded predecessor-DAG enumeration).
//
// The synthetic test runs on the same timetable as `lb_raptor_test.cc`. The
// real-timetable benchmark is disabled by default, run it with:
//
//   NIGIRI_TT=<path/to/tt.bin> ./nigiri-test
//     --gtest_also_run_disabled_tests
//     --gtest_filter='*transfers_lb_raptor_real*'
//
// Measured on DELFI (nationwide, 2026-09-01, 3 days: 500515 locations,
// 177361 routes -> 170419 lb routes), best of 5 per query, gcc-14 -O3.
// Four Berlin ontrip queries, bidir_lb_raptor -> transfers_bidir_lb_raptor:
//
//   search only (NIGIRI_MAX_PATTERNS=0, no reconstruction)
//     786 -> 234ms | 572 -> 136ms | 461 -> 69ms | 432 -> 79ms
//     i.e. 3.4x - 6.6x faster. The first-touch BFS is the whole win.
//
//   end to end, one chain per meetpoint (NIGIRI_MAX_PREDS=1 NIGIRI_MAX_CHAINS=1)
//     946 -> 629ms | 809 -> 593ms | 572 -> 747ms | 502 -> 1560ms
//     i.e. 1.5x / 1.4x faster, 0.8x / 0.3x slower. The search win is eaten by
//     realization: transfer-optimal patterns have longer hops, so
//     get_alternative has more work per pattern even at equal pattern counts.
//
//   end to end, default caps (3 preds/step, 4 chains/meetpoint)
//     946 -> 1148ms | 809 -> 2096ms | 572 -> 1775ms | 502 -> 2456ms
//     0.2x - 0.8x, but 4x the patterns (590 -> 2268) and journeys
//     (361 -> 1549). The DAG enumeration buys alternatives, not speed.
//
// Quality: both recover the same share of the plain-RAPTOR pareto front, and
// best-arrival per transfer count agrees up to ~2 transfers. Beyond that the
// transfers-only patterns are noticeably worse (hours later, or missing) -
// the transport count alone cannot tell a good predecessor from a bad one.
//
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

#include "gtest/gtest.h"

#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"
#include "nigiri/routing/lb_raptor/transfers_bidir_lb_raptor.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/rt/create_rt_timetable.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

#include "../util.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;

// defined in lb_raptor_test.cc
mem_dir lb_test_tt();

namespace {

constexpr auto kTestDateRange = interval{sys_days{2026_y / February / 27},
                                         sys_days{2026_y / February / 28}};

using trip_key = std::tuple<unixtime_t, unixtime_t, std::uint8_t>;

std::set<trip_key> to_keys(std::vector<journey> const& js) {
  auto s = std::set<trip_key>{};
  for (auto const& j : js) {
    s.emplace(j.departure_time(), j.arrival_time(), j.transfers_);
  }
  return s;
}

location_idx_t find_by_name(timetable const& tt, std::string_view const name) {
  for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
    if (tt.get_default_name(l) == name && tt.locations_.get_root_idx(l) == l) {
      return l;
    }
  }
  return location_idx_t::invalid();
}

}  // namespace

TEST(routing, transfers_bidir_lb_raptor) {
  auto const tt = load_gtfs(lb_test_tt, kTestDateRange);
  auto rtt = rt::create_rt_timetable(tt, sys_days{2026_y / February / 27});

  auto const from = tt.locations_.location_id_to_idx_.at({"P", source_idx_t{0U}});
  auto const to = tt.locations_.location_id_to_idx_.at({"T", source_idx_t{0U}});
  auto const q =
      query{.start_time_ = unixtime_t{sys_days{February / 27 / 2026}},
            .start_ = {{from, 3_minutes, 0U}},
            .destination_ = {{to, 13_minutes, 0U}}};

  auto ref = bidir_lb_raptor{};
  ref.execute(tt, &rtt, q);

  auto lbr = transfers_bidir_lb_raptor{};
  lbr.execute(tt, &rtt, q);

  auto const print = [&](std::string_view const title,
                         std::vector<journey> const& js) {
    auto ss = std::stringstream{};
    for (auto const& j : js) {
      j.print(ss, tt, &rtt);
      ss << "\n";
    }
    fmt::println("{} ({} journeys):\n{}", title, js.size(), ss.str());
  };
  print("bidir_lb_raptor", ref.journeys_);
  print("transfers_bidir_lb_raptor", lbr.journeys_);

  EXPECT_FALSE(lbr.journeys_.empty());

  // The terminals label themselves with 0 transports.
  EXPECT_EQ(0U, lbr.fwd_lb_[from]);
  EXPECT_EQ(0U, lbr.bwd_lb_[to]);

  // Core property: the transport count is a *lower* bound, so no journey any
  // algorithm finds may use fewer transports than the label says. The label is
  // the number of transports, a journey reports transfers = transports - 1.
  //
  // Both searches stop at the line where they meet, so the far terminal is not
  // necessarily labelled - checked where a label exists.
  auto const check_lb = [](std::uint8_t const lb,
                           std::vector<journey> const& js) {
    if (lb == transfers_bidir_lb_raptor::kUnreachableLb) {
      return;
    }
    for (auto const& j : js) {
      EXPECT_LE(lb, j.transfers_ + 1U);
    }
  };
  check_lb(lbr.fwd_lb_[to], ref.journeys_);
  check_lb(lbr.fwd_lb_[to], lbr.journeys_);
  check_lb(lbr.bwd_lb_[from], ref.journeys_);
  check_lb(lbr.bwd_lb_[from], lbr.journeys_);

  // Every meetpoint that produced a pattern carries a label from both sides.
  for (auto const& p : lbr.patterns_) {
    EXPECT_NE(location_idx_t::invalid(), p[0]);
  }
  EXPECT_FALSE(lbr.patterns_.empty());

  fmt::println(
      "patterns: bidir={} transfers={} | journeys: bidir={} transfers={}",
      ref.patterns_.size(), lbr.patterns_.size(), ref.journeys_.size(),
      lbr.journeys_.size());
}

TEST(routing, DISABLED_transfers_lb_raptor_real_timetable) {
  auto const* const path = std::getenv("NIGIRI_TT");
  ASSERT_NE(nullptr, path) << "set NIGIRI_TT to a tt.bin";

  auto const tt_ptr = timetable::read(std::filesystem::path{path});
  auto const& tt = *tt_ptr;
  fmt::println("timetable: {} locations, {} routes, {} lb routes, {}",
               tt.n_locations(), tt.n_routes(),
               tt.lb_route_times_[profile_idx_t{0U}].size(),
               tt.external_interval());

  struct od {
    std::string_view from_, to_;
  };
  auto const queries = std::vector<od>{
      {"S+U Alexanderplatz", "S+U Berlin Hauptbahnhof"},
      {"S+U Berlin Hauptbahnhof", "S Potsdam Hauptbahnhof"},
      {"S+U Berlin Hauptbahnhof", "S Ostkreuz Bhf (Berlin)"},
      {"S+U Berlin Hauptbahnhof", "S Bernau Bhf"},
      {"S Spandau Bhf (Berlin)", "S Grünau (Berlin)"},
  };

  auto const day = tt.external_interval().from_ + std::chrono::hours{24 + 8};

  auto ref = bidir_lb_raptor{};
  auto lbr = transfers_bidir_lb_raptor{};
  if (auto const* const n = std::getenv("NIGIRI_MAX_PATTERNS"); n != nullptr) {
    ref.max_patterns_per_round_ = static_cast<unsigned>(std::atoi(n));
    lbr.max_patterns_per_round_ = static_cast<unsigned>(std::atoi(n));
  }
  if (auto const* const n = std::getenv("NIGIRI_MAX_PREDS"); n != nullptr) {
    lbr.max_preds_per_step_ = static_cast<unsigned>(std::atoi(n));
  }
  if (auto const* const n = std::getenv("NIGIRI_MAX_CHAINS"); n != nullptr) {
    lbr.max_chains_per_meetpoint_ = static_cast<unsigned>(std::atoi(n));
  }
  if (std::getenv("NIGIRI_NO_TIME_ORDER") != nullptr) {
    lbr.order_preds_by_time_ = false;
  }
  fmt::println(
      "max_patterns_per_round={} | max_preds_per_step={} "
      "max_chains_per_meetpoint={} max_patterns_per_meetpoint={} "
      "order_preds_by_time={}",
      lbr.max_patterns_per_round_, lbr.max_preds_per_step_,
      lbr.max_chains_per_meetpoint_, lbr.max_patterns_per_meetpoint_,
      lbr.order_preds_by_time_);

  auto s_state = search_state{};
  auto r_state = raptor_state{};

  for (auto const& [from_name, to_name] : queries) {
    auto const from = find_by_name(tt, from_name);
    auto const to = find_by_name(tt, to_name);
    if (from == location_idx_t::invalid() || to == location_idx_t::invalid()) {
      fmt::println("\n### SKIP {} -> {} (not found)", from_name, to_name);
      continue;
    }

    auto const* const iv_min = std::getenv("NIGIRI_INTERVAL_MIN");
    auto const start_time =
        iv_min == nullptr
            ? start_time_t{day}
            : start_time_t{interval<unixtime_t>{
                  day, day + duration_t{static_cast<std::int16_t>(
                           std::atoi(iv_min))}}};
    auto q = query{.start_time_ = start_time,
                   .start_match_mode_ = location_match_mode::kEquivalent,
                   .dest_match_mode_ = location_match_mode::kEquivalent,
                   .start_ = {{from, 0_minutes, 0U}},
                   .destination_ = {{to, 0_minutes, 0U}}};

    fmt::println("\n### {} -> {}", from_name, to_name);

    auto const bench = [&](auto& algo) {
      auto dt = std::chrono::steady_clock::duration::max();
      for (auto rep = 0U; rep != 5U; ++rep) {
        auto const t0 = std::chrono::steady_clock::now();
        algo.execute(tt, nullptr, q);
        dt = std::min(dt, std::chrono::steady_clock::now() - t0);
      }
      return std::chrono::duration_cast<std::chrono::microseconds>(dt).count();
    };

    auto const ref_us = bench(ref);
    auto const lbr_us = bench(lbr);

    fmt::println(
        "bidir_lb_raptor           {:8.2f}ms | {:5} reconstructions -> {:4} "
        "truncated, {:5} repetitions, {:4} unrealizable, {:5} pruned | {:4} "
        "patterns -> {:4} journeys",
        static_cast<double>(ref_us) / 1000.0,
        ref.stats_.pattern_reconstructions_, ref.stats_.truncated_patterns_,
        ref.stats_.pattern_repetitions_, ref.stats_.unrealizable_patterns_,
        ref.stats_.pruned_meetpoints_, ref.patterns_.size(),
        ref.journeys_.size());
    fmt::println(
        "transfers_bidir_lb_raptor {:8.2f}ms | {:5} reconstructions -> {:4} "
        "truncated, {:5} repetitions, {:4} unrealizable, {:5} pruned | {:4} "
        "patterns -> {:4} journeys",
        static_cast<double>(lbr_us) / 1000.0,
        lbr.stats_.pattern_reconstructions_, lbr.stats_.truncated_patterns_,
        lbr.stats_.pattern_repetitions_, lbr.stats_.unrealizable_patterns_,
        lbr.stats_.pruned_meetpoints_, lbr.patterns_.size(),
        lbr.journeys_.size());
    fmt::println(
        "  transfers extra: {} chains, {} pred expansions, {} dead ends | "
        "speedup {:.2f}x",
        lbr.stats_.chain_enumerations_, lbr.stats_.pred_expansions_,
        lbr.stats_.dead_ends_,
        lbr_us == 0 ? 0.0
                    : static_cast<double>(ref_us) / static_cast<double>(lbr_us));

    // Reference: plain RAPTOR. Everything it finds is on the pareto front, so
    // it is the yardstick for how much of the front each variant recovers.
    auto const t1 = std::chrono::steady_clock::now();
    auto const raptor_res =
        raptor_search(tt, nullptr, s_state, r_state, q, direction::kForward);
    auto const raptor_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - t1)
                               .count();

    auto raptor_keys = std::set<trip_key>{};
    for (auto const& j : *raptor_res.journeys_) {
      raptor_keys.emplace(j.departure_time(), j.arrival_time(), j.transfers_);
    }
    auto const ref_keys = to_keys(ref.journeys_);
    auto const lbr_keys = to_keys(lbr.journeys_);

    auto const covered = [&](std::set<trip_key> const& k) {
      auto n = 0U;
      for (auto const& r : raptor_keys) {
        if (k.contains(r)) {
          ++n;
        }
      }
      return n;
    };

    fmt::println("raptor                    {:8.2f}ms | {} journeys",
                 static_cast<double>(raptor_us) / 1000.0,
                 raptor_res.journeys_->size());
    fmt::println(
        "  pareto coverage: bidir {}/{} | transfers {}/{}  (distinct "
        "dep/arr/transfers: bidir {}, transfers {})",
        covered(ref_keys), raptor_keys.size(), covered(lbr_keys),
        raptor_keys.size(), ref_keys.size(), lbr_keys.size());

    // Best arrival per transfer count - the quality signal that matters when
    // the travel-time labels are dropped.
    auto const best_per_transfers = [](std::vector<journey> const& js) {
      auto best = std::map<std::uint8_t, unixtime_t>{};
      for (auto const& j : js) {
        auto const it = best.find(j.transfers_);
        if (it == end(best) || j.arrival_time() < it->second) {
          best[j.transfers_] = j.arrival_time();
        }
      }
      return best;
    };
    auto const ref_best = best_per_transfers(ref.journeys_);
    auto const lbr_best = best_per_transfers(lbr.journeys_);
    auto all_t = std::set<std::uint8_t>{};
    for (auto const& [t, _] : ref_best) {
      all_t.insert(t);
    }
    for (auto const& [t, _] : lbr_best) {
      all_t.insert(t);
    }
    for (auto const t : all_t) {
      auto const r = ref_best.find(t);
      auto const l = lbr_best.find(t);
      fmt::println("  BEST transfers={} bidir={} transfers_only={}", t,
                   r == end(ref_best) ? "-" : fmt::format("{}", r->second),
                   l == end(lbr_best) ? "-" : fmt::format("{}", l->second));
    }
  }
}
