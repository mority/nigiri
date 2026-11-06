#include "gtest/gtest.h"

#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "fmt/format.h"

#include "nigiri/loader/load.h"
#include "nigiri/routing/one_to_n.h"
#include "nigiri/routing/one_to_n_accumulators.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"

using namespace nigiri;
using namespace nigiri::routing;
using namespace date;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {

// Times are local (Europe/Berlin, UTC+2 on 2019-05-01).
// Stops are far apart: no generated footpaths between them.
//   R1 A->B: 10:00-10:10, 10:30-10:40
//   R2 B->C: 10:15-10:25, 10:45-10:55
//   R3 A->C: 10:05-10:40, 10:35-11:10 (slow, no transfer)
//   R4 C->D: 10:30-10:45, 11:00-11:15
//   R5 B->D: 10:20-11:00
//   R6 D->E: 11:20-11:30
//   R7 B->A: 10:12-10:22
constexpr auto const kFiles = R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Europe/Berlin

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,48.0,8.0,,
B,B,,48.5,8.0,,
C,C,,49.0,8.0,,
D,D,,49.5,8.0,,
E,E,,50.0,8.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,1,,,3
R2,DB,2,,,3
R3,DB,3,,,3
R4,DB,4,,,3
R5,DB,5,,,3
R6,DB,6,,,3
R7,DB,7,,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S1,T1a,,
R1,S1,T1b,,
R2,S1,T2a,,
R2,S1,T2b,,
R3,S1,T3a,,
R3,S1,T3b,,
R4,S1,T4a,,
R4,S1,T4b,,
R5,S1,T5a,,
R6,S1,T6,,
R7,S1,T7,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence
T1a,10:00:00,10:00:00,A,0
T1a,10:10:00,10:10:00,B,1
T1b,10:30:00,10:30:00,A,0
T1b,10:40:00,10:40:00,B,1
T2a,10:15:00,10:15:00,B,0
T2a,10:25:00,10:25:00,C,1
T2b,10:45:00,10:45:00,B,0
T2b,10:55:00,10:55:00,C,1
T3a,10:05:00,10:05:00,A,0
T3a,10:40:00,10:40:00,C,1
T3b,10:35:00,10:35:00,A,0
T3b,11:10:00,11:10:00,C,1
T4a,10:30:00,10:30:00,C,0
T4a,10:45:00,10:45:00,D,1
T4b,11:00:00,11:00:00,C,0
T4b,11:15:00,11:15:00,D,1
T5a,10:20:00,10:20:00,B,0
T5a,11:00:00,11:00:00,D,1
T6,11:20:00,11:20:00,D,0
T6,11:30:00,11:30:00,E,1
T7,10:12:00,10:12:00,B,0
T7,10:22:00,10:22:00,A,1

# calendar_dates.txt
service_id,date,exception_type
S1,20190501,1
)"sv;

timetable load_tt() {
  return loader::load(loader::mem_dir::read(kFiles),
                      {2019_y / May / 1, 2019_y / May / 2});
}

location_idx_t stop(timetable const& tt, std::string_view id) {
  return tt.locations_.location_id_to_idx_.at(
      {.id_ = id, .src_ = source_idx_t{0U}});
}

// local time on 2019-05-01 -> UTC
unixtime_t local(int const h, int const m) {
  return unixtime_t{sys_days{2019_y / May / 1}} + std::chrono::hours{h - 2} +
         std::chrono::minutes{m};
}

//  c0: A 5     c1: B 3, C 8     c2: C 2     c3: D 4, B 30
//  c4: none    c5: A 1          c6: E 3
std::vector<std::vector<stop_walk>> make_access(timetable const& tt) {
  auto const w = [&](std::string_view id, int const min) {
    return stop_walk{.location_ = stop(tt, id), .duration_ = duration_t{min}};
  };
  return {{w("A", 5)}, {w("B", 3), w("C", 8)},
          {w("C", 2)}, {w("D", 4), w("B", 30)},
          {},          {w("A", 1)},
          {w("E", 3)}};
}

constexpr auto const kNCells = 7U;

using label_map = std::map<std::tuple<cell_idx_t, cell_idx_t, unixtime_t>,
                           std::vector<one_to_n_label>>;

struct collector final : public one_to_n_accumulator {
  collector(label_map& m, std::mutex& mutex) : m_{m}, mutex_{mutex} {}

  std::unique_ptr<one_to_n_accumulator> clone() const override {
    return std::make_unique<collector>(m_, mutex_);
  }
  void begin_origin(cell_idx_t const o) override { origin_ = o; }
  void add_sample(unixtime_t const dep, one_to_n_sample const& s) override {
    auto const lock = std::lock_guard{mutex_};
    for (auto const c : s.reached()) {
      auto const labels = s[c];
      m_[{origin_, c, dep}] = {begin(labels), end(labels)};
    }
  }
  void end_origin(cell_idx_t) override {}

  label_map& m_;
  std::mutex& mutex_;
  cell_idx_t origin_{0U};
};

std::vector<one_to_n_label> labels(timetable const& tt,
                                   one_to_n_targets const& targets,
                                   one_to_n_query q,
                                   cell_idx_t const o,
                                   cell_idx_t const d,
                                   unixtime_t const dep) {
  q.origins_ = {o};
  q.departures_ = {dep};
  auto m = label_map{};
  auto mutex = std::mutex{};
  one_to_n(tt, nullptr, targets, q, collector{m, mutex});
  auto const it = m.find({o, d, dep});
  return it == end(m) ? std::vector<one_to_n_label>{} : it->second;
}

std::vector<one_to_n_label> pareto(
    std::map<std::uint8_t, duration_t> const& best_per_k) {
  auto front = std::vector<one_to_n_label>{};
  for (auto const& [k, travel_time] : best_per_k) {
    if (front.empty() || travel_time < front.back().travel_time_) {
      front.push_back({.k_ = k, .travel_time_ = travel_time});
    }
  }
  return front;
}

// Reference: point-to-point intermodal search with the same walks.
std::vector<one_to_n_label> reference(timetable const& tt,
                                      one_to_n_targets const& targets,
                                      cell_idx_t const o,
                                      cell_idx_t const d,
                                      unixtime_t const dep,
                                      duration_t const max_travel_time) {
  auto const to_offsets = [](auto const& walks) {
    auto offsets = std::vector<offset>{};
    for (auto const& w : walks) {
      offsets.emplace_back(w.location_, w.duration_,
                           transport_mode_t::payload_t{0U});
    }
    return offsets;
  };
  if (targets.access_[o].empty() || targets.egress_by_cell_[d].empty()) {
    return {};
  }

  auto q = query{};
  q.start_time_ = dep;
  q.start_match_mode_ = location_match_mode::kIntermodal;
  q.dest_match_mode_ = location_match_mode::kIntermodal;
  q.start_ = to_offsets(targets.access_[o]);
  q.destination_ = to_offsets(targets.egress_by_cell_[d]);
  q.max_travel_time_ = max_travel_time;
  // one_to_n has walk-only labels only from the direct cell walks: don't
  // prune with walks through a stop (start -> stop -> destination)
  q.fastest_direct_ = kMaxTravelTime;

  auto search_state = routing::search_state{};
  auto algo_state = raptor_state{};
  auto const result = raptor_search(tt, nullptr, search_state, algo_state, q,
                                    direction::kForward);

  auto best = std::map<std::uint8_t, duration_t>{};
  for (auto const& j : *result.journeys_) {
    auto const travel_time =
        std::chrono::duration_cast<duration_t>(j.arrival_time() - dep);
    if (travel_time > max_travel_time) {
      continue;
    }
    auto const k = static_cast<std::uint8_t>(j.transfers_ + 1U);
    auto const it = best.find(k);
    if (it == end(best) || travel_time < it->second) {
      best[k] = travel_time;
    }
  }
  return pareto(best);
}

std::vector<unixtime_t> departures() {
  return {local(9, 50),  local(10, 0), local(10, 20),
          local(10, 40), local(11, 0), local(11, 10)};
}

std::string fmt_labels(std::vector<one_to_n_label> const& labels) {
  auto s = std::string{};
  for (auto const& l : labels) {
    s += fmt::format("({}, {}) ", l.k_, l.travel_time_.count());
  }
  return s;
}

}  // namespace

TEST(one_to_n, pareto_sets) {
  auto const tt = load_tt();
  auto const targets = make_one_to_n_targets(tt, make_access(tt));
  auto q = one_to_n_query{};
  q.max_travel_time_ = 90_minutes;

  auto const dep = local(9, 50);
  auto const c = [](std::uint32_t const i) { return cell_idx_t{i}; };
  auto const l = [](std::uint8_t const k, int const t) {
    return one_to_n_label{.k_ = k, .travel_time_ = duration_t{t}};
  };

  // c0 -> c1: T1a A 10:00 -> B 10:10, walk 3 -> 10:13
  EXPECT_EQ((std::vector{l(1, 23)}), labels(tt, targets, q, c(0), c(1), dep));
  // c0 -> c2: T3a A 10:05 -> C 10:40, walk 2 -> 10:42
  //           T1a, T2a B 10:15 -> C 10:25, walk 2 -> 10:27
  EXPECT_EQ((std::vector{l(1, 52), l(2, 37)}),
            labels(tt, targets, q, c(0), c(2), dep));
  // c0 -> c3: T1a to B 10:10, walk 30 -> 10:40 beats all rides to D
  EXPECT_EQ((std::vector{l(1, 50)}), labels(tt, targets, q, c(0), c(3), dep));
  // c0 -> c4: no stops
  EXPECT_TRUE(labels(tt, targets, q, c(0), c(4), dep).empty());
  // c0 -> c5: T1a, T7 B 10:12 -> A 10:22, walk 1 -> 10:23
  EXPECT_EQ((std::vector{l(2, 33)}), labels(tt, targets, q, c(0), c(5), dep));
  // c0 -> c6: T5a B 10:20 -> D 11:00, T6 -> E 11:30, walk 3 -> 11:33 (103 min)
  EXPECT_TRUE(labels(tt, targets, q, c(0), c(6), dep).empty());
  q.max_travel_time_ = 103_minutes;
  EXPECT_EQ((std::vector{l(3, 103)}), labels(tt, targets, q, c(0), c(6), dep));
  q.max_travel_time_ = 102_minutes;
  EXPECT_TRUE(labels(tt, targets, q, c(0), c(6), dep).empty());
}

TEST(one_to_n, walk_only_labels) {
  auto const tt = load_tt();
  auto direct = std::vector<std::vector<cell_walk>>(kNCells);
  direct[0].push_back({.cell_ = cell_idx_t{5U}, .duration_ = duration_t{6}});
  direct[0].push_back({.cell_ = cell_idx_t{1U}, .duration_ = duration_t{40}});
  direct[0].push_back({.cell_ = cell_idx_t{0U}, .duration_ = duration_t{0}});
  auto const targets = make_one_to_n_targets(tt, make_access(tt), {}, direct);
  auto q = one_to_n_query{};
  q.max_travel_time_ = 90_minutes;

  auto const dep = local(9, 50);
  auto const l = [](std::uint8_t const k, int const t) {
    return one_to_n_label{.k_ = k, .travel_time_ = duration_t{t}};
  };

  // walking (6 min) beats every ride
  EXPECT_EQ((std::vector{l(0, 6)}),
            labels(tt, targets, q, cell_idx_t{0U}, cell_idx_t{5U}, dep));
  // walking (40 min) is slower: the ride (23 min) is kept
  EXPECT_EQ((std::vector{l(0, 40), l(1, 23)}),
            labels(tt, targets, q, cell_idx_t{0U}, cell_idx_t{1U}, dep));
}

// Every (origin, destination, departure) against the intermodal search, with
// and without the origin lower bounds, single- and multi-threaded.
TEST(one_to_n, matches_intermodal_search) {
  auto const tt = load_tt();
  auto const targets = make_one_to_n_targets(tt, make_access(tt));

  for (auto const max_travel_time : {45_minutes, 90_minutes, 150_minutes}) {
    auto expected = label_map{};
    for (auto o = 0U; o != kNCells; ++o) {
      for (auto d = 0U; d != kNCells; ++d) {
        if (o == d) {
          continue;
        }
        for (auto const dep : departures()) {
          auto ref = reference(tt, targets, cell_idx_t{o}, cell_idx_t{d}, dep,
                               max_travel_time);
          if (!ref.empty()) {
            expected[{cell_idx_t{o}, cell_idx_t{d}, dep}] = std::move(ref);
          }
        }
      }
    }
    ASSERT_FALSE(expected.empty());

    for (auto const prune : {true, false}) {
      for (auto const n_threads : {1U, 3U}) {
        auto q = one_to_n_query{};
        q.departures_ = departures();
        q.max_travel_time_ = max_travel_time;
        q.prune_unreachable_cells_ = prune;
        q.n_threads_ = n_threads;

        auto actual = label_map{};
        auto mutex = std::mutex{};
        auto const stats =
            one_to_n(tt, nullptr, targets, q, collector{actual, mutex});

        EXPECT_EQ(kNCells, stats.n_origins_);
        EXPECT_EQ(kNCells * departures().size(), stats.n_searches_);

        for (auto const& [key, ref] : expected) {
          auto const it = actual.find(key);
          auto const& [o, d, dep] = key;
          EXPECT_TRUE(it != end(actual) && it->second == ref)
              << "o=" << to_idx(o) << ", d=" << to_idx(d) << ", dep=" << dep
              << ", max_travel_time=" << max_travel_time.count()
              << ", prune=" << prune << ": expected " << fmt_labels(ref)
              << ", got "
              << (it == end(actual) ? "nothing" : fmt_labels(it->second));
        }
        for (auto const& [key, got] : actual) {
          auto const& [o, d, dep] = key;
          EXPECT_TRUE(expected.contains(key))
              << "o=" << to_idx(o) << ", d=" << to_idx(d) << ", dep=" << dep
              << ", max_travel_time=" << max_travel_time.count()
              << ", prune=" << prune << ": unexpected " << fmt_labels(got);
        }
      }
    }
  }
}

TEST(one_to_n, target_pruning_bound) {
  auto const tt = load_tt();
  auto const targets = make_one_to_n_targets(tt, make_access(tt));

  auto q = one_to_n_query{};
  q.origins_ = {cell_idx_t{0U}};
  q.departures_ = {local(9, 50)};

  // 150 min: every cell with a stop is reached from c0 -> the bound applies.
  // (c4 has no stops and is ruled out by the lower bound.)
  auto m = label_map{};
  auto mutex = std::mutex{};
  q.max_travel_time_ = 150_minutes;
  auto const reached_all =
      one_to_n(tt, nullptr, targets, q, collector{m, mutex});
  EXPECT_GT(reached_all.n_bound_rounds_, 0U);

  // 90 min: c6 (103 min) is not reached but not ruled out by the lower bound.
  q.max_travel_time_ = 90_minutes;
  auto const reached_some =
      one_to_n(tt, nullptr, targets, q, collector{m, mutex});
  EXPECT_EQ(0U, reached_some.n_bound_rounds_);

  // c4 has no stops: nothing to search.
  q.origins_ = {cell_idx_t{4U}};
  auto const isolated = one_to_n(tt, nullptr, targets, q, collector{m, mutex});
  EXPECT_EQ(1U, isolated.n_searches_skipped_);
}

TEST(one_to_n, raw_writer) {
  auto const tt = load_tt();
  auto const targets = make_one_to_n_targets(tt, make_access(tt));

  auto q = one_to_n_query{};
  q.origins_ = {cell_idx_t{0U}};
  q.departures_ = {local(9, 50)};
  q.max_travel_time_ = 90_minutes;

  auto out = std::stringstream{};
  auto mutex = std::mutex{};
  auto const stats =
      one_to_n(tt, nullptr, targets, q, one_to_n_raw_writer{out, mutex});

  EXPECT_EQ(
      "0,1,2019-05-01T07:50Z,1,23\n"
      "0,2,2019-05-01T07:50Z,1,52\n"
      "0,2,2019-05-01T07:50Z,2,37\n"
      "0,3,2019-05-01T07:50Z,1,50\n"
      "0,5,2019-05-01T07:50Z,2,33\n",
      out.str());
  EXPECT_EQ(5U, stats.n_labels_);
}

TEST(one_to_n, histogram) {
  auto const tt = load_tt();
  auto const targets = make_one_to_n_targets(tt, make_access(tt));

  auto q = one_to_n_query{};
  q.origins_ = {cell_idx_t{0U}};
  q.departures_ = {local(10, 20), local(9, 50)};
  q.max_travel_time_ = 90_minutes;

  auto const config = one_to_n_histogram::config{.n_cells_ = kNCells,
                                                 .bin_width_ = duration_t{10},
                                                 .n_bins_ = 6U,
                                                 .max_k_ = 3U};
  EXPECT_EQ(
      "origin,destination,k,n_samples,unreached,bin_0,bin_1,bin_2,bin_3,bin_4,"
      "bin_5\n",
      one_to_n_histogram::header(config));

  auto out = std::stringstream{};
  auto mutex = std::mutex{};
  one_to_n(tt, nullptr, targets, q, one_to_n_histogram{config, out, mutex});

  // c0 -> c2 at 09:50 and 10:20: (1, 52 min), (2, 37 min) both times.
  auto lines = std::vector<std::string>{};
  auto line = std::string{};
  while (std::getline(out, line)) {
    if (line.starts_with("0,2,")) {
      lines.push_back(line);
    }
  }
  EXPECT_EQ((std::vector<std::string>{"0,2,1,2,0,0,0,0,0,0,2",
                                      "0,2,2,2,0,0,0,0,2,0,0",
                                      "0,2,3,2,0,0,0,0,2,0,0"}),
            lines);
}
