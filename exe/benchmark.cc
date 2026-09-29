#include <cstdio>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <regex>
#include <span>
#include <thread>

#include "boost/program_options.hpp"

#include "utl/helpers/algorithm.h"
#include "utl/parallel_for.h"
#include "utl/parser/cstr.h"
#include "utl/progress_tracker.h"
#include "utl/zip.h"

#include "nigiri/logging.h"
#include "nigiri/qa/qa.h"
#include "nigiri/query_generator/generator.h"
#include "nigiri/routing/alternatives.h"
#include "nigiri/routing/interval_estimate.h"
#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "nigiri/routing/gpu/raptor.h"

#ifndef _WIN32
#include <sys/resource.h>
#endif

using namespace nigiri;
using namespace nigiri::routing;

std::vector<std::string> tokenize(std::string_view const str,
                                  char const delimiter) {
  auto tokens = std::vector<std::string>{};
  utl::for_each_token(
      utl::cstr{str.data(), str.size()}, delimiter,
      [&](utl::cstr const t) { tokens.emplace_back(t.str, t.len); });
  return tokens;
}

std::optional<geo::box> parse_bbox(std::string const& str) {
  using namespace geo;

  if (str == "europe") {
    return box{latlng{36.0, -11.0}, latlng{72.0, 32.0}};
  }

  static auto const bbox_regex = std::regex{
      "^[-+]?[0-9]*\\.?[0-9]+,[-+]?[0-9]*\\.?[0-9]+,[-+]?[0-9]*\\.?[0-9]+,[-+]?"
      "[0-9]*\\.?[0-9]+$"};
  if (!std::regex_match(begin(str), end(str), bbox_regex)) {
    return std::nullopt;
  }
  auto const tokens = tokenize(str, ',');
  return box{latlng{std::stod(tokens[0]), std::stod(tokens[1])},
             latlng{std::stod(tokens[2]), std::stod(tokens[3])}};
}

std::optional<geo::latlng> parse_coord(std::string const& str) {
  using namespace geo;

  static auto const coord_regex =
      std::regex{R"(^\([-+]?[0-9]*\.?[0-9]+, [-+]?[0-9]*\.?[0-9]+\))"};
  if (!std::regex_match(begin(str), end(str), coord_regex)) {
    return std::nullopt;
  }
  auto const str_trimmed = std::string_view{begin(str) + 1, end(str) - 2};
  auto const tokens = tokenize(str_trimmed, ',');
  return latlng{std::stod(tokens[0]), std::stod(tokens[1])};
}

void generate_queries(
    std::vector<nigiri::query_generation::start_dest_query>& queries,
    std::uint32_t n_queries,
    nigiri::timetable const& tt,
    query_generation::generator_settings const& gs,
    std::int64_t const seed) {
  auto qg = seed > -1
                ? query_generation::generator{tt, gs,
                                              static_cast<std::uint32_t>(seed)}
                : query_generation::generator{tt, gs};
  queries.reserve(n_queries);
  for (auto i = 0U; i != n_queries; ++i) {
    auto const sdq = qg.random_query();
    if (sdq.has_value()) {
      queries.emplace_back(sdq.value());
    }
  }
}

// Range-RAPTOR start time == first trip's departure time
// Pong start time != first trip's departure time
// -> travel time is not measured from departure
// -> give Pong some slack
constexpr auto const kCheckedMaxTravelTime = routing::kMaxTravelTime - 1_days;

std::uint64_t compare_results(
    timetable const& tt,
    std::string const& ref_name,
    std::vector<pareto_set<routing::journey>> const& ref,
    std::string const& cmp_name,
    std::vector<pareto_set<routing::journey>> const& cmp,
    std::vector<nigiri::query_generation::start_dest_query> const& queries,
    direction const search_dir,
    unsigned const min_connection_count) {
  auto mismatches = std::uint64_t{0U};

  auto const equal = [](journey const& a, journey const& b) {
    return a.start_time_ == b.start_time_ && a.dest_time_ == b.dest_time_ &&
           a.transfers_ == b.transfers_;
  };

  auto const key = [](journey const& j) {
    return fmt::format("dep={} arr={} transfers={}", j.departure_time(),
                       j.arrival_time(), j.transfers_);
  };

  auto const max_window = [&](query const& q) {
    return search_dir == direction::kForward
               ? interval_estimator<direction::kForward>{tt, q}.max_interval()
               : interval_estimator<direction::kBackward>{tt, q}.max_interval();
  };

  auto const filtered = [](pareto_set<routing::journey> const& set,
                           interval<unixtime_t> const& window) {
    auto v = std::vector<journey const*>{};
    for (auto const& j : set) {
      if (j.travel_time() < kCheckedMaxTravelTime /* slack for pong */ &&
          window.contains(j.start_time_) /* range raptor search limit */) {
        v.push_back(&j);
      }
    }
    return v;
  };

  auto const print_set = [&](std::string const& name, auto const& journeys) {
    fmt::print("  {}: ", name);
    for (auto const* j : journeys) {
      fmt::print("[{}] ", key(*j));
    }
    fmt::println("");
  };

  for (auto i = std::size_t{0U}; i != ref.size(); ++i) {
    auto const window = max_window(queries[i].q_);
    auto r = filtered(ref[i], window);
    auto c = filtered(cmp[i], window);
    if (search_dir == direction::kBackward) {
      std::reverse(begin(r), end(r));
      std::reverse(begin(c), end(c));
    }

    auto const r_size = r.size();
    auto const c_size = c.size();
    auto const n = std::min(r_size, c_size);
    auto const r_zip = std::span{r.data(), n};
    auto const c_zip = std::span{c.data(), n};

    // Count under-deliverying results:
    // >= min_connection_count reached by one but not the other
    auto const raw_r = ref[i].size();
    auto const raw_c = cmp[i].size();
    auto misses = std::uint64_t{0U};
    if (r_size >= min_connection_count && raw_c < min_connection_count) {
      misses += min_connection_count - raw_c;
    }
    if (c_size >= min_connection_count && raw_r < min_connection_count) {
      misses += min_connection_count - raw_r;
    }

    // Count inequalities.
    for (auto const [a, b] : utl::zip(r_zip, c_zip)) {
      if (!equal(*a, *b)) {
        ++misses;
      }
    }

    if (misses != 0U) {
      fmt::println("query #{} mismatches={} ({} n={}, {} n={})", i, misses,
                   ref_name, r_size, cmp_name, c_size);
      print_set(ref_name, r);
      print_set(cmp_name, c);
      for (auto const [a, b] : utl::zip(r_zip, c_zip)) {
        if (!equal(*a, *b)) {
          fmt::println("  === MISMATCH: {} [{}] vs {} [{}] ===", ref_name,
                       key(*a), cmp_name, key(*b));
          a->print(std::cout, tt);
          fmt::println("");
          b->print(std::cout, tt);
          fmt::println("");
        }
      }
    }

    mismatches += misses;
  }

  return mismatches;
}

struct cpu_ws {
  search_state ss_;
  routing::raptor_state rs_;
  std::map<std::string, std::uint64_t> last_stats_;
};

// second best statistics: per stop counters live in the raptor state
template <typename WS>
void reset_sb_counters(WS& w) {
  if constexpr (std::is_same_v<WS, cpu_ws>) {
    utl::fill(w.rs_.sb_second_, 0U);
    utl::fill(w.rs_.sb_evicted_, 0U);
  }
}

// one worker thread per state, pulling queries from a shared counter
template <typename WS, typename SearchFn>
std::vector<double> run_load(
    std::vector<nigiri::query_generation::start_dest_query> const& queries,
    std::string const& tag,
    std::vector<WS*> const& states,
    SearchFn search_one) {
  if (!queries.empty()) {
    // Warm up (allocate search state).
    for (auto* s : states) {
      search_one(*s, queries.front().q_, std::size_t{0U});
      reset_sb_counters(*s);
    }
  }

  auto next = std::atomic<std::size_t>{0};
  auto done = std::atomic<std::size_t>{0};
  auto lat = std::vector<double>(queries.size(), -1.0);
  auto const t0 = std::chrono::steady_clock::now();
  auto workers = std::vector<std::thread>{};
  for (auto* ws : states) {
    workers.emplace_back([&, ws]() {
      for (auto i = next.fetch_add(1); i < queries.size();
           i = next.fetch_add(1)) {
        try {
          auto const q0 = std::chrono::steady_clock::now();
          search_one(*ws, queries[i].q_, i);
          lat[i] = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - q0)
                       .count();
          done.fetch_add(1);
        } catch (std::exception const& e) {
          std::cerr << "q#" << i << " FAILED: " << e.what() << std::endl;
        }
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }
  auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  auto const d = done.load();
  auto const qps =
      d * 1000.0 / static_cast<double>(std::max<std::int64_t>(ms, 1));

  auto l = lat;
  std::erase_if(l, [](double const x) { return x < 0.0; });
  std::sort(begin(l), end(l));
  auto const q = [&](double const p) {
    return l.empty() ? 0.0
                     : l[std::min(l.size() - 1,
                                  static_cast<std::size_t>(p * l.size()))];
  };
  auto const avg =
      l.empty() ? 0.0 : std::accumulate(begin(l), end(l), 0.0) / l.size();
  fmt::print(
      "| {:<36} | {:>6.1f} | {:>6.0f} | {:>6.0f} | {:>6.0f} | "
      "{:>6.0f} |\n",
      tag, qps, avg, q(0.50), q(0.90), q(0.99));
  return lat;
}

struct result_set {
  std::string label_;
  std::vector<pareto_set<routing::journey>> res_;
  std::vector<double> latencies_;
  std::vector<std::map<std::string, std::uint64_t>> stats_;
  std::vector<std::uint64_t> sb_second_;
  std::vector<std::uint64_t> sb_evicted_;
};

#if defined(NIGIRI_CUDA)
struct gpu_ws {
  explicit gpu_ws(routing::gpu::gpu_timetable const& gtt)
      : rs_{std::make_unique<routing::gpu::gpu_raptor_state>(gtt)} {}
  search_state ss_;
  std::unique_ptr<routing::gpu::gpu_raptor_state> rs_;
};
#endif

// one (engine, algo) cell: runs the queries once per n_parallel value (each
// worker borrows a state from a pool allocated once at the maximum count)
// and keeps the last run's journeys + latencies
template <typename WS, typename Search, typename... StateArgs>
result_set run_cell(
    std::vector<nigiri::query_generation::start_dest_query> const& queries,
    std::string const& label,
    std::vector<unsigned> const& n_parallel,
    Search&& search,
    StateArgs const&... state_args) {
  auto out = result_set{.label_ = label};
  out.res_.resize(queries.size());
  out.stats_.resize(queries.size());

  auto states = std::vector<std::unique_ptr<WS>>{};
  for (auto i = 0U; i != *std::max_element(begin(n_parallel), end(n_parallel));
       ++i) {
    states.push_back(std::make_unique<WS>(state_args...));
  }

  for (auto const n : n_parallel) {
    auto pool = std::vector<WS*>{};
    for (auto i = 0U; i != n; ++i) {
      pool.push_back(states[i].get());
    }
    out.latencies_ =
        run_load<WS>(queries, label + "-" + std::to_string(n), pool,
                     [&](WS& w, routing::query q, std::size_t const i) {
                       out.res_[i] = search(w, std::move(q));
                       if constexpr (std::is_same_v<WS, cpu_ws>) {
                         out.stats_[i] = w.last_stats_;
                       }
                     });
    if constexpr (std::is_same_v<WS, cpu_ws>) {
      out.sb_second_.clear();
      out.sb_evicted_.clear();
      for (auto const* w : pool) {
        out.sb_second_.resize(w->rs_.sb_second_.size());
        out.sb_evicted_.resize(w->rs_.sb_evicted_.size());
        for (auto l = 0U; l != w->rs_.sb_second_.size(); ++l) {
          out.sb_second_[l] += w->rs_.sb_second_[l];
          out.sb_evicted_[l] += w->rs_.sb_evicted_[l];
        }
      }
    }
  }

  return out;
}

void print_sb_stats(timetable const& tt,
                    result_set const& cell,
                    std::filesystem::path const& csv_path) {
  auto sum = std::map<std::string, std::uint64_t>{};
  for (auto const& m : cell.stats_) {
    for (auto const& [k, v] : m) {
      sum[k] += v;
    }
  }

  fmt::println("\n--- second best: {} ---", cell.label_);
  for (auto const& [k, v] : sum) {
    fmt::println("{:<40} {:>14}", k, v);
  }

  auto n_queries_with_second = 0U;
  for (auto const& m : cell.stats_) {
    auto const it = m.find("n_sb_dest_second");
    n_queries_with_second += it != end(m) && it->second != 0U ? 1U : 0U;
  }
  fmt::println("{:<40} {:>14}", "queries with second time at dest",
               n_queries_with_second);

  auto stops = std::vector<std::uint32_t>{};
  for (auto l = 0U; l != cell.sb_second_.size(); ++l) {
    if (cell.sb_second_[l] + cell.sb_evicted_[l] != 0U) {
      stops.push_back(l);
    }
  }
  utl::sort(stops, [&](std::uint32_t const a, std::uint32_t const b) {
    return cell.sb_second_[a] + cell.sb_evicted_[a] >
           cell.sb_second_[b] + cell.sb_evicted_[b];
  });
  fmt::println("{:<40} {:>14}", "stops with second best label",
               stops.size());
  fmt::println("{:<40} {:>14}", "stops", tt.n_locations());

  fmt::println("\n{:>10} {:>10}  {}", "second", "evicted", "stop");
  for (auto const l : std::span{stops}.first(
           std::min(stops.size(), std::size_t{25U}))) {
    fmt::println("{:>10} {:>10}  {}", cell.sb_second_[l], cell.sb_evicted_[l],
                 loc{tt, location_idx_t{l}});
  }

  if (!csv_path.empty()) {
    auto out = std::ofstream{csv_path};
    out << "location_idx,id,name,lat,lon,second,evicted\n";
    for (auto const l : stops) {
      auto const idx = location_idx_t{l};
      auto const pos = tt.locations_.coordinates_[idx];
      out << fmt::format("{},\"{}\",\"{}\",{},{},{},{}\n", l,
                         tt.locations_.ids_[idx].view(),
                         tt.get_default_name(idx), pos.lat_, pos.lng_,
                         cell.sb_second_[l], cell.sb_evicted_[l]);
    }
  }
}

// Reference for alternatives: block routes / transfer stations of the fastest
// journey, search again, compare what comes back to the fastest journey.
void print_compact(timetable const& tt, journey const& j) {
  fmt::println("  {} -> {}, {} transfers", j.departure_time(),
               j.arrival_time(), j.transfers_);
  for (auto const& l : j.legs_) {
    auto const name =
        routing::is_transit(l)
            ? std::string{tt.transport_name(
                  std::get<journey::run_enter_exit>(l.uses_).r_.t_.t_idx_)}
            : std::string{"walk"};
    static auto const with_ids = std::getenv("NIGIRI_PRINT_IDS") != nullptr;
    fmt::println("    {} {:<40} -> {} {:<40} {}",
                 date::format("%d. %H:%M", l.dep_time_),
                 tt.get_default_name(l.from_),
                 date::format("%d. %H:%M", l.arr_time_),
                 tt.get_default_name(l.to_), name);
    if (with_ids) {
      fmt::println(
          "      {} -> {} {}", tt.locations_.ids_[l.from_].view(),
          tt.locations_.ids_[l.to_].view(),
          routing::is_transit(l)
              ? fmt::format(
                    "{}",
                    std::get<journey::run_enter_exit>(l.uses_).r_.t_)
              : std::string{});
    }
  }
}

void run_alternatives(
    timetable const& tt,
    std::vector<nigiri::query_generation::start_dest_query> const& queries,
    direction const dir,
    unsigned const n_threads,
    unsigned const n_examples,
    std::filesystem::path const& csv_path) {
  using routing::alternative;
  using routing::alternatives;
  using routing::block_kind;
  using crit_t = std::pair<int, int>;  // minutes later than ref, transfers

  auto res = std::vector<alternatives>(queries.size());
  auto next = std::atomic<std::size_t>{0};
  auto workers = std::vector<std::thread>{};
  auto const t0 = std::chrono::steady_clock::now();
  for (auto t = 0U; t != n_threads; ++t) {
    workers.emplace_back([&]() {
      auto w = cpu_ws{};
      for (auto i = next.fetch_add(1); i < queries.size();
           i = next.fetch_add(1)) {
        try {
          res[i] = routing::find_alternatives(tt, nullptr, w.ss_, w.rs_,
                                              queries[i].q_, dir);
        } catch (std::exception const& e) {
          std::cerr << "q#" << i << " FAILED: " << e.what() << std::endl;
        }
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }
  auto const seconds = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - t0)
                           .count();

  auto const transfer_stations = [&](journey const& j) {
    auto v = std::vector<location_idx_t>{};
    auto first = true;
    for (auto const& l : j.legs_) {
      if (routing::is_transit(l)) {
        if (!first) {
          v.push_back(tt.locations_.get_root_idx(l.from_));
        }
        first = false;
      }
    }
    return v;
  };
  auto const transports = [&](journey const& j) {
    auto v = std::vector<transport>{};
    for (auto const& l : j.legs_) {
      if (routing::is_transit(l)) {
        v.push_back(std::get<journey::run_enter_exit>(l.uses_).r_.t_);
      }
    }
    return v;
  };

  constexpr auto const kNBuckets = 6U;
  constexpr auto const kBuckets =
      std::array{"<= 5", "<= 15", "<= 30", "<= 60", "> 60", "none"};
  auto const bucket = [](int const delta) {
    return delta <= 5    ? 0U
           : delta <= 15 ? 1U
           : delta <= 30 ? 2U
           : delta <= 60 ? 3U
                         : 4U;
  };

  // structure of an alternative compared to the fastest journey
  constexpr auto const kNStructures = 4U;
  constexpr auto const kStructure =
      std::array{"other stations, other last trip",
                 "other stations, same last trip",
                 "same stations, other last trip",
                 "same stations, same last trip"};
  auto const get_structure = [&](journey const& ref, journey const& alt) {
    auto const same_stations =
        transfer_stations(ref) == transfer_stations(alt);
    auto const same_last = transports(ref).back() == transports(alt).back();
    return (same_stations ? 2U : 0U) + (same_last ? 1U : 0U);
  };

  // where an alternative joins the fastest journey (common suffix of trips)
  // and what a search needs to keep to find it
  constexpr auto const kNMerges = 5U;
  constexpr auto const kMerge = std::array{
      "no common suffix (other last trip)",
      "enters the common trip at another stop",
      "same stop, same time at stop",
      "same stop, later at stop",
      "same trips (other stops / footpaths)"};
  auto const get_merge = [&](journey const& ref, journey const& alt) {
    auto const legs = [](journey const& j) {
      auto v = std::vector<journey::leg const*>{};
      for (auto const& l : j.legs_) {
        if (routing::is_transit(l)) {
          v.push_back(&l);
        }
      }
      return v;
    };
    auto const t = [](journey::leg const* l) {
      return std::get<journey::run_enter_exit>(l->uses_).r_.t_;
    };
    auto const r = legs(ref);
    auto const a = legs(alt);
    auto n = 0U;
    while (n != r.size() && n != a.size() &&
           t(r[r.size() - 1U - n]) == t(a[a.size() - 1U - n])) {
      ++n;
    }
    if (n == 0U) {
      return 0U;
    }
    if (n == r.size() && n == a.size()) {
      return 4U;
    }
    auto const rl = r[r.size() - n];
    auto const al = a[a.size() - n];
    if (rl->from_ != al->from_) {
      return 1U;
    }
    // time at the stop = arrival of the leg before the common trip
    auto const before = [](journey const& j, journey::leg const* l) {
      return l == &j.legs_.front() ? j.start_time_ : (l - 1)->arr_time_;
    };
    return before(ref, rl) == before(alt, al) ? 2U : 3U;
  };
  auto equal_merge = std::array<unsigned, kNMerges>{};
  auto later_15_merge = std::array<unsigned, kNMerges>{};

  auto n_ref = 0U, n_with_alt = 0U, n_searches = 0U, n_alts = 0U;
  auto n_two_best = 0U, n_queries_two_best = 0U;
  auto n_equal = 0U;  // queries: alternative with time + transfers of ref
  auto equal_structure = std::array<unsigned, kNStructures>{};
  auto later = std::array<unsigned, kNBuckets>{};
  auto later_structure =
      std::array<std::array<unsigned, kNStructures>, kNBuckets>{};
  auto later_kind = std::array<std::array<unsigned, 2>, kNBuckets>{};
  auto later_more = std::array<unsigned, kNBuckets>{};  // more transfers
  auto examples = std::vector<std::pair<std::size_t, alternative const*>>{};
  auto equal_examples =
      std::vector<std::pair<std::size_t, alternative const*>>{};

  auto csv = std::ofstream{};
  if (!csv_path.empty()) {
    csv.open(csv_path);
    csv << "query,ref_travel_time,ref_transfers,n_base,n_alts,n_searches,"
           "n_two_best,equal_structure,later_delta,later_structure,"
           "later_blocked,later_more_transfers_delta\n";
  }

  for (auto const [i, a] : utl::enumerate(res)) {
    if (!a.ref_.has_value()) {
      continue;
    }
    ++n_ref;
    n_searches += a.n_searches_;
    n_alts += static_cast<unsigned>(a.alts_.size());
    n_with_alt += a.alts_.empty() ? 0U : 1U;

    auto const& ref = *a.ref_;
    auto const crit = [&](journey const& j) {
      return crit_t{
          static_cast<int>((j.travel_time() - ref.travel_time()).count()),
          static_cast<int>(j.transfers_)};
    };
    auto const dominates = [](crit_t const& x, crit_t const& y) {
      return x != y && x.first <= y.first && x.second <= y.second;
    };

    auto base = std::vector<crit_t>{};
    for (auto const& j : a.base_) {
      base.push_back(crit(j));
    }
    auto all = base;
    for (auto const& x : a.alts_) {
      all.push_back(crit(x.j_));
    }
    std::sort(begin(all), end(all));
    all.erase(std::unique(begin(all), end(all)), end(all));

    // criteria a search with two labels per stop should return in addition
    auto two_best = 0U;
    for (auto const& c : all) {
      if (utl::find(base, c) == end(base) &&
          utl::count_if(all, [&](crit_t const& x) {
            return dominates(x, c);
          }) == 1) {
        ++two_best;
      }
    }
    n_two_best += two_best;
    n_queries_two_best += two_best != 0U ? 1U : 0U;

    // closest alternative: equal to ref / later than ref with the transfers
    // of ref / later than ref with more transfers
    auto eq = static_cast<alternative const*>(nullptr);
    auto lt = static_cast<alternative const*>(nullptr);
    auto more = static_cast<alternative const*>(nullptr);
    auto has_equal = std::array<bool, kNMerges>{};
    auto has_later_15 = std::array<bool, kNMerges>{};
    for (auto const& x : a.alts_) {
      auto const c = crit(x.j_);
      if (c.second == ref.transfers_ && c.first <= 15) {
        (c.first == 0 ? has_equal : has_later_15)[get_merge(ref, x.j_)] = true;
      }
      if (c.second == ref.transfers_ && c.first == 0) {
        if (eq == nullptr ||
            get_structure(ref, x.j_) < get_structure(ref, eq->j_)) {
          eq = &x;
        }
      } else if (c.second == ref.transfers_) {
        if (lt == nullptr || c < crit(lt->j_)) {
          lt = &x;
        }
      } else if (c.second > ref.transfers_) {
        if (more == nullptr || c < crit(more->j_)) {
          more = &x;
        }
      }
    }

    for (auto m = 0U; m != kNMerges; ++m) {
      equal_merge[m] += has_equal[m] ? 1U : 0U;
      later_15_merge[m] += has_later_15[m] ? 1U : 0U;
    }

    if (eq != nullptr) {
      ++n_equal;
      ++equal_structure[get_structure(ref, eq->j_)];
      if (equal_examples.size() < n_examples) {
        equal_examples.emplace_back(i, eq);
      }
    }
    auto const b = lt == nullptr ? kNBuckets - 1U : bucket(crit(lt->j_).first);
    ++later[b];
    ++later_more[more == nullptr ? kNBuckets - 1U
                                 : bucket(crit(more->j_).first)];
    if (lt != nullptr) {
      auto const s = get_structure(ref, lt->j_);
      ++later_structure[b][s];
      ++later_kind[b][lt->kind_ == block_kind::kRoute ? 0U : 1U];
      if (b <= 1U && s == 0U && examples.size() < n_examples) {
        examples.emplace_back(i, lt);
      }
    }

    if (csv.is_open()) {
      csv << fmt::format(
          "{},{},{},{},{},{},{},{},{},{},{},{}\n", i,
          ref.travel_time().count(), ref.transfers_, a.base_.size(),
          a.alts_.size(), a.n_searches_, two_best,
          eq == nullptr ? "" : kStructure[get_structure(ref, eq->j_)],
          lt == nullptr ? "" : std::to_string(crit(lt->j_).first),
          lt == nullptr ? "" : kStructure[get_structure(ref, lt->j_)],
          lt == nullptr
              ? ""
              : lt->kind_ == block_kind::kRoute ? "route" : "transfer",
          more == nullptr ? "" : std::to_string(crit(more->j_).first));
    }
  }

  fmt::println("\n--- alternatives by blocking: {} queries, {:.0f} s ---",
               queries.size(), seconds);
  fmt::println("{:<52} {:>8}", "queries with journey", n_ref);
  fmt::println("{:<52} {:>8}", "searches", n_searches);
  fmt::println("{:<52} {:>8}", "alternative journeys", n_alts);
  fmt::println("{:<52} {:>8}", "queries with alternative", n_with_alt);
  fmt::println("{:<52} {:>8}", "criteria with exactly one dominator",
               n_two_best);
  fmt::println("{:<52} {:>8}", "queries with such criteria",
               n_queries_two_best);

  fmt::println(
      "\nalternative with arrival and transfers of the fastest journey");
  fmt::println("{:<52} {:>8}", "queries", n_equal);
  for (auto s = 0U; s != kNStructures; ++s) {
    fmt::println("  {:<50} {:>8}", kStructure[s], equal_structure[s]);
  }

  fmt::println(
      "\nclosest later alternative, minutes after the fastest journey\n"
      "st = transfer stations, lt = last trip, o = other, s = same\n"
      "{:<8} {:>10} | {:>8} {:>8} {:>8} {:>8} | {:>8} {:>8} | {:>10}",
      "", "same tr.", "o st o lt", "o st s lt", "s st o lt", "s st s lt",
      "by route", "by transf", "more tr.");
  for (auto b = 0U; b != kNBuckets; ++b) {
    fmt::println(
        "{:<8} {:>10} | {:>9} {:>9} {:>9} {:>9} | {:>8} {:>9} | {:>10}",
        kBuckets[b], later[b], later_structure[b][0], later_structure[b][1],
        later_structure[b][2], later_structure[b][3], later_kind[b][0],
        later_kind[b][1], later_more[b]);
  }

  fmt::println(
      "\nqueries with an alternative (transfers of the fastest journey) that "
      "joins the fastest journey as follows\n{:<52} {:>8} {:>10}",
      "", "equal", "<= 15 min");
  for (auto m = 0U; m != kNMerges; ++m) {
    fmt::println("{:<52} {:>8} {:>10}", kMerge[m], equal_merge[m],
                 later_15_merge[m]);
  }

  auto const print_examples = [&](auto const& v, char const* title) {
    for (auto const& [i, x] : v) {
      fmt::println("\n=== query #{}: fastest ===", i);
      print_compact(tt, *res[i].ref_);
      fmt::println("--- {}, found by blocking {} {} ---", title,
                   x->kind_ == block_kind::kRoute ? "route of transit leg"
                                                  : "transfer",
                   x->idx_);
      print_compact(tt, x->j_);
    }
  };
  print_examples(equal_examples, "equal alternative");
  print_examples(examples, "later alternative");
}

// Walking time of a journey = duration of all its footpath legs (transfers
// at the same stop included). Compares the journey of each result with the
// alternatives that follow it.
void print_walking(timetable const& tt,
                   std::vector<pareto_set<routing::journey>> const& res,
                   unsigned const n_examples) {
  auto const walking = [](journey const& j) {
    auto sum = 0;
    for (auto const& l : j.legs_) {
      if (std::holds_alternative<footpath>(l.uses_)) {
        sum += static_cast<int>((l.arr_time_ - l.dep_time_).count());
      }
    }
    return sum;
  };
  // walking between different stops only
  auto const walking_between_stops = [](journey const& j) {
    auto sum = 0;
    for (auto const& l : j.legs_) {
      if (std::holds_alternative<footpath>(l.uses_) && l.from_ != l.to_) {
        sum += static_cast<int>((l.arr_time_ - l.dep_time_).count());
      }
    }
    return sum;
  };

  struct cmp {
    unsigned n_results_{0U}, n_with_alt_{0U};
    unsigned n_shorter_{0U}, n_equal_{0U}, n_longer_{0U};
    std::array<unsigned, 5> saved_{};  // 1-2, 3-5, 6-10, 11-20, > 20
    std::uint64_t sum_base_{0U}, sum_best_{0U};  // results with alternative
    std::uint64_t sum_base_all_{0U};
    std::vector<std::tuple<int, journey const*, journey const*>> examples_;
  };

  auto const run = [&](auto const& get_walking, char const* title) {
    auto all = cmp{}, fastest = cmp{};
    for (auto const& set : res) {
      auto fastest_it = end(set);
      for (auto it = begin(set); it != end(set); ++it) {
        if (it->alternative_ == 0U &&
            (fastest_it == end(set) ||
             it->travel_time() < fastest_it->travel_time())) {
          fastest_it = it;
        }
      }

      for (auto it = begin(set); it != end(set); ++it) {
        if (it->alternative_ != 0U) {
          continue;
        }
        auto const base = get_walking(*it);
        auto best = static_cast<journey const*>(nullptr);
        for (auto a = std::next(it); a != end(set) && a->alternative_ != 0U;
             ++a) {
          if (best == nullptr || get_walking(*a) < get_walking(*best)) {
            best = &*a;
          }
        }

        auto const add = [&](cmp& c) {
          ++c.n_results_;
          c.sum_base_all_ += static_cast<unsigned>(base);
          if (best == nullptr) {
            return;
          }
          ++c.n_with_alt_;
          auto const w = get_walking(*best);
          c.sum_base_ += static_cast<unsigned>(base);
          c.sum_best_ += static_cast<unsigned>(std::min(base, w));
          if (w < base) {
            ++c.n_shorter_;
            auto const d = base - w;
            ++c.saved_[d <= 2 ? 0U : d <= 5 ? 1U : d <= 10 ? 2U
                                          : d <= 20 ? 3U : 4U];
            c.examples_.emplace_back(d, &*it, best);
          } else if (w == base) {
            ++c.n_equal_;
          } else {
            ++c.n_longer_;
          }
        };
        add(all);
        if (it == fastest_it) {
          add(fastest);
        }
      }
    }

    auto const print = [&](cmp const& c, char const* name) {
      auto const avg = [](std::uint64_t const sum, unsigned const n) {
        return n == 0U ? 0.0 : static_cast<double>(sum) / n;
      };
      fmt::println(
          "\n--- {}: {} ---\n"
          "{:<52} {:>8}\n{:<52} {:>8.1f}\n{:<52} {:>8}\n"
          "{:<52} {:>8}\n{:<52} {:>8}\n{:<52} {:>8}\n"
          "{:<52} {:>8.1f}\n{:<52} {:>8.1f}\n"
          "{:<52} {:>8}\n{:<52} {:>8}\n{:<52} {:>8}\n{:<52} {:>8}\n"
          "{:<52} {:>8}",
          title, name,  //
          "results", c.n_results_,  //
          "avg minutes, all results", avg(c.sum_base_all_, c.n_results_),
          "results with alternative", c.n_with_alt_,  //
          "  best alternative shorter", c.n_shorter_,  //
          "  best alternative equal", c.n_equal_,  //
          "  best alternative longer", c.n_longer_,  //
          "  avg minutes, journey", avg(c.sum_base_, c.n_with_alt_),
          "  avg minutes, best of journey and alternatives",
          avg(c.sum_best_, c.n_with_alt_),  //
          "  shorter by 1-2 min", c.saved_[0],  //
          "  shorter by 3-5 min", c.saved_[1],  //
          "  shorter by 6-10 min", c.saved_[2],  //
          "  shorter by 11-20 min", c.saved_[3],  //
          "  shorter by > 20 min", c.saved_[4]);
    };
    print(all, "all results");
    print(fastest, "fastest journey of each query");

    utl::sort(all.examples_, [](auto const& a, auto const& b) {
      return std::get<0>(a) > std::get<0>(b);
    });
    // every tenth: not only the extreme ones
    for (auto i = 0U; i < all.examples_.size() && i / 10U < n_examples;
         i += 10U) {
      auto const& [d, j, alt] = all.examples_[i];
      fmt::println("\n=== {} min shorter: journey ===", d);
      print_compact(tt, *j);
      fmt::println("--- alternative ---");
      print_compact(tt, *alt);
    }
  };

  run(walking, "walking time incl. transfers at the same stop");
  run(walking_between_stops, "walking time between different stops");
}

// Journeys from the extra slot compared to the best journey with the same
// number of transfers.
void print_extra_slot(timetable const& tt,
                      result_set const& cell,
                      unsigned const n_examples) {
  auto const trips = [](journey const& j) {
    auto v = std::vector<transport>{};
    for (auto const& l : j.legs_) {
      if (routing::is_transit(l)) {
        v.push_back(std::get<journey::run_enter_exit>(l.uses_).r_.t_);
      }
    }
    return v;
  };

  auto n_best = 0U, n_slot = 0U, n_queries = 0U, n_fastest = 0U;
  auto n_same_trips = 0U, n_same_last = 0U, n_same_first = 0U,
       n_no_shared = 0U;
  auto delta = std::array<unsigned, 6>{};  // <= 1, 5, 15, 30, 60, > 60
  auto examples = std::vector<std::pair<journey const*, journey const*>>{};
  for (auto const& set : cell.res_) {
    auto any = false;
    auto fastest = static_cast<journey const*>(nullptr);
    for (auto const& j : set) {
      if (j.slot_ == 0U && j.alternative_ == 0U) {
        ++n_best;
        if (fastest == nullptr || j.travel_time() < fastest->travel_time()) {
          fastest = &j;
        }
      }
    }
    for (auto const& j : set) {
      if (j.slot_ == 0U || j.alternative_ != 0U) {
        continue;
      }
      ++n_slot;
      any = true;
      auto const best =
          utl::find_if(set, [&](journey const& x) {
            return x.slot_ == 0U && x.alternative_ == 0U &&
                   x.transfers_ == j.transfers_;
          });
      if (best == end(set)) {
        continue;
      }
      n_fastest += &*best == fastest ? 1U : 0U;
      auto const d = std::abs((j.dest_time_ - best->dest_time_).count());
      ++delta[d <= 1 ? 0U : d <= 5 ? 1U : d <= 15 ? 2U : d <= 30 ? 3U
                                                  : d <= 60 ? 4U : 5U];
      auto const a = trips(*best);
      auto const b = trips(j);
      if (a == b) {
        ++n_same_trips;
      } else if (!a.empty() && !b.empty() && a.back() == b.back()) {
        ++n_same_last;
      } else if (!a.empty() && !b.empty() && a.front() == b.front()) {
        ++n_same_first;
      } else {
        ++n_no_shared;
      }
      if (a != b && examples.size() < n_examples * 20U) {
        examples.emplace_back(&*best, &j);
      }
    }
    n_queries += any ? 1U : 0U;
  }

  auto sum = std::map<std::string, std::uint64_t>{};
  for (auto const& m : cell.stats_) {
    for (auto const& [k, v] : m) {
      sum[k] += v;
    }
  }

  fmt::println(
      "\n--- extra slot: {} ---\n"
      "{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n"
      "{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n"
      "{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n"
      "{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n{:<52} {:>10}\n"
      "{:<52} {:>10}\n{:<52} {:>10}",
      cell.label_,  //
      "journeys, best slot", n_best,  //
      "journeys, extra slot", n_slot,  //
      "extra slot journeys of the search", sum["n_slot_journeys"],  //
      "queries with extra slot journey", n_queries,  //
      "queries, fastest journey has one", n_fastest,  //
      "labels written, best", sum["n_earliest_arrival_updated_by_footpath"],
      "labels written, extra slot", sum["n_slot_labels"],  //
      "labels rejected, extra slot taken", sum["n_slot_rejects"],  //
      "extra slot <= 1 min after best", delta[0],  //
      "  <= 5 min", delta[1],  //
      "  <= 15 min", delta[2],  //
      "  <= 30 min", delta[3],  //
      "  <= 60 min", delta[4],  //
      "  > 60 min", delta[5],  //
      "same trips as the best journey", n_same_trips,  //
      "other trips, same last trip", n_same_last,  //
      "other trips, same first trip", n_same_first,  //
      "other trips, other first and last trip", n_no_shared);

  for (auto i = 0U; i < examples.size(); i += 20U) {
    fmt::println("\n=== best ===");
    print_compact(tt, *examples[i].first);
    fmt::println("--- extra slot ---");
    print_compact(tt, *examples[i].second);
  }
}

// Equal alternatives of the fastest journey: blocking (reference) against
// traversal of the extra slot tables. For each alternative only the
// reference has: where does it leave the tables?
void compare_alternatives(
    timetable const& tt,
    std::vector<nigiri::query_generation::start_dest_query> const& queries,
    direction const dir,
    unsigned const n_threads,
    unsigned const n_examples) {
  enum reason : unsigned {
    kFound,
    kSameTrips,
    kThird,
    kBeyond255,
    kFewerTransfers,
    kNoBestInRound,
    kOtherNotStored,
    kLabelsPresent,
    kNReasons
  };
  constexpr auto const kReason = std::array{
      "found by the traversal",
      "same trips as the fastest journey",
      "a label is behind both times of its stop",
      "a label is more than 255 min behind the best",
      "a label is not earlier than one with fewer transfers",
      "a label has no best time of its round to follow",
      "a label is not stored, other reason",
      "all labels in the tables, not reached"};

  struct cmp {
    bool has_ref_{false};
    unsigned n_traversal_{0U};  // journeys with criteria of the fastest
    std::array<unsigned, kNReasons> n_{};
    std::vector<std::pair<reason, routing::journey>> examples_;
    std::optional<routing::journey> ref_;
  };

  using trips_t = std::vector<transport>;
  auto const trips = [](journey const& j) {
    auto v = trips_t{};
    for (auto const& l : j.legs_) {
      if (routing::is_transit(l)) {
        v.push_back(std::get<journey::run_enter_exit>(l.uses_).r_.t_);
      }
    }
    return v;
  };

  constexpr auto const kFwd = true;
  utl::verify(dir == direction::kForward, "compare: forward only");
  constexpr auto const kInvalid = kInvalidDelta<direction::kForward>;

  auto res = std::vector<cmp>(queries.size());
  auto next = std::atomic<std::size_t>{0};
  auto workers = std::vector<std::thread>{};
  for (auto t = 0U; t != n_threads; ++t) {
    workers.emplace_back([&]() {
      auto w = cpu_ws{};
      for (auto i = next.fetch_add(1); i < queries.size();
           i = next.fetch_add(1)) {
        try {
          auto& c = res[i];
          auto q = queries[i].q_;
          q.extra_slot_ = false;
          q.n_alternatives_ = 1U;
          auto const ref =
              routing::find_alternatives(tt, nullptr, w.ss_, w.rs_, q, dir);
          if (!ref.ref_.has_value()) {
            continue;
          }
          c.has_ref_ = true;
          c.ref_ = ref.ref_;

          q.extra_slot_ = true;
          q.n_alternatives_ = routing::kUnlimitedAlternatives;
          q.alternatives_tree_ = true;
          auto const r =
              routing::raptor_search(tt, nullptr, w.ss_, w.rs_, q, dir);
          auto found = std::vector<trips_t>{};
          for (auto const& j : *r.journeys_) {
            if (j.dest_time_ == ref.ref_->dest_time_ &&
                j.transfers_ == ref.ref_->transfers_) {
              found.push_back(trips(j));
            }
          }
          c.n_traversal_ = static_cast<unsigned>(found.size());

          // tables of the extra slot search
          auto const start = std::get<unixtime_t>(q.start_time_);
          auto const base = std::chrono::time_point_cast<date::days>(
              std::chrono::round<std::chrono::days>(start));
          auto const round_times =
              std::as_const(w.rs_).get_round_times<0U>();
          auto const get = [&](unsigned const k, location_idx_t const l) {
            auto const b = round_times[k][to_idx(l)][0];
            auto const d = w.rs_.round_delta(k, to_idx(l));
            return std::pair{
                b, d == 0U || b == kInvalid
                       ? kInvalid
                       : static_cast<delta_t>(b + (kFwd ? 1 : -1) * d)};
          };

          auto const ref_trips = trips(*ref.ref_);
          for (auto const& a : ref.alts_) {
            if (a.j_.dest_time_ != ref.ref_->dest_time_ ||
                a.j_.transfers_ != ref.ref_->transfers_) {
              continue;
            }
            auto const t = trips(a.j_);
            auto why = kLabelsPresent;
            if (t == ref_trips) {
              why = kSameTrips;
            } else if (utl::find(found, t) != end(found)) {
              why = kFound;
            } else {
              // labels at the stops where a trip is entered, last one first
              auto legs = std::vector<journey::leg const*>{};
              for (auto const& l : a.j_.legs_) {
                if (routing::is_transit(l)) {
                  legs.push_back(&l);
                }
              }
              for (auto k = static_cast<unsigned>(legs.size()) - 1U; k != 0U;
                   --k) {
                auto const leg = legs[k];
                auto const time = unix_to_delta(
                    base, leg == &a.j_.legs_.front() ? leg->dep_time_
                                                     : (leg - 1)->arr_time_);
                auto const [best, later] = get(k, leg->from_);
                if (time == best || time == later) {
                  continue;
                }
                auto fewer = kInvalid;
                for (auto x = 0U; x != k; ++x) {
                  fewer = std::min(fewer, get(x, leg->from_).first);
                }
                why = best == kInvalid && fewer <= time ? kFewerTransfers
                      : best == kInvalid               ? kNoBestInRound
                      : later != kInvalid && time > later ? kThird
                      : time - best > 255               ? kBeyond255
                      : fewer <= time                   ? kFewerTransfers
                                                        : kOtherNotStored;
                break;
              }
            }
            ++c.n_[why];
            if (why != kFound && why != kSameTrips && c.examples_.empty()) {
              c.examples_.emplace_back(why, a.j_);
            }
          }
        } catch (std::exception const& e) {
          std::cerr << "q#" << i << " FAILED: " << e.what() << std::endl;
        }
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }

  auto n_ref = 0U, n_ref_equal = 0U, n_ref_equal_other_trips = 0U;
  auto n_traversal = 0U, n_both = 0U, n_only_ref = 0U, n_only_traversal = 0U;
  auto alts = std::array<unsigned, kNReasons>{};
  auto only_ref = std::array<unsigned, kNReasons>{};  // queries, by reason
  auto printed = std::array<unsigned, kNReasons>{};
  for (auto const& c : res) {
    if (!c.has_ref_) {
      continue;
    }
    ++n_ref;
    auto n_equal = 0U;
    for (auto r = 0U; r != kNReasons; ++r) {
      alts[r] += c.n_[r];
      n_equal += c.n_[r];
    }
    auto const ref_has = n_equal != 0U;
    auto const ref_has_other = n_equal - c.n_[kSameTrips] != 0U;
    auto const trav_has = c.n_traversal_ > 1U;
    n_ref_equal += ref_has ? 1U : 0U;
    n_ref_equal_other_trips += ref_has_other ? 1U : 0U;
    n_traversal += trav_has ? 1U : 0U;
    n_both += ref_has && trav_has ? 1U : 0U;
    n_only_traversal += !ref_has && trav_has ? 1U : 0U;
    if (ref_has && !trav_has) {
      ++n_only_ref;
      // reason of the query = reason of its first alternative not found
      auto const why = c.examples_.empty() ? kSameTrips
                                           : c.examples_.front().first;
      ++only_ref[why];
    }
  }

  fmt::println(
      "\n--- equal alternatives of the fastest journey: blocking vs. "
      "traversal ---\n"
      "{:<60} {:>8}\n{:<60} {:>8}\n{:<60} {:>8}\n{:<60} {:>8}\n"
      "{:<60} {:>8}\n{:<60} {:>8}\n{:<60} {:>8}",
      "queries with journey", n_ref,  //
      "blocking has an equal alternative", n_ref_equal,  //
      "  ... with other trips", n_ref_equal_other_trips,  //
      "traversal has an equal alternative", n_traversal,  //
      "both", n_both,  //
      "only blocking", n_only_ref,  //
      "only traversal", n_only_traversal);

  fmt::println("\n{:<60} {:>8} {:>14}", "equal alternatives of blocking",
               "journeys", "only blocking");
  for (auto r = 0U; r != kNReasons; ++r) {
    fmt::println("{:<60} {:>8} {:>14}", kReason[r], alts[r], only_ref[r]);
  }

  for (auto const [i, c] : utl::enumerate(res)) {
    if (!c.has_ref_ || c.n_traversal_ > 1U || c.examples_.empty()) {
      continue;
    }
    auto const& [why, j] = c.examples_.front();
    if (printed[why]++ >= n_examples) {
      continue;
    }
    fmt::println("\n=== query #{}: fastest ===", i);
    print_compact(tt, *c.ref_);
    fmt::println("--- only blocking: {} ---", kReason[why]);
    print_compact(tt, j);
  }
}

// Walking time of the journeys with the same number of transfers:
// today = the journey today's reconstruction returns (best arrival)
// tie   = least walking among the journeys with the best arrival
// slot  = least walking among the journeys of the extra slot (later arrival)
void print_walking_slot(std::vector<pareto_set<routing::journey>> const& res,
                        timetable const& tt,
                        unsigned const n_examples) {
  auto const run = [&](bool const between_stops, char const* title) {
    auto const walking = [&](journey const& j) {
      auto sum = 0;
      for (auto const& l : j.legs_) {
        if (std::holds_alternative<footpath>(l.uses_) &&
            (!between_stops || l.from_ != l.to_)) {
          sum += static_cast<int>((l.arr_time_ - l.dep_time_).count());
        }
      }
      return sum;
    };

    struct group {
      journey const* first_{nullptr};
      journey const* least_{nullptr};
    };
    constexpr auto const kSaved = std::array{1, 3, 6, 11};
    constexpr auto const kLater = std::array{5, 15, 30, 60, 100000};
    auto n_best = 0U, n_tie = 0U, n_pairs = 0U;
    auto n_slot_vs_today = 0U, n_slot_vs_tie = 0U, n_queries = 0U;
    auto sum_today = 0U, sum_tie = 0U, sum_any = 0U;
    auto table = std::array<std::array<unsigned, 5>, 4>{};
    auto examples =
        std::vector<std::tuple<int, journey const*, journey const*>>{};

    for (auto const& set : res) {
      auto groups = std::map<std::pair<unsigned, unsigned>, group>{};
      for (auto const& j : set) {
        auto& g = groups[{j.transfers_, j.slot_}];
        if (j.alternative_ == 0U) {
          g.first_ = &j;
        }
        if (g.least_ == nullptr || walking(j) < walking(*g.least_)) {
          g.least_ = &j;
        }
      }

      auto any = false;
      for (auto const& [key, best] : groups) {
        if (key.second != 0U || best.first_ == nullptr) {
          continue;
        }
        ++n_best;
        auto const today = walking(*best.first_);
        auto const tie = walking(*best.least_);
        n_tie += tie < today ? 1U : 0U;
        sum_today += static_cast<unsigned>(today);
        sum_tie += static_cast<unsigned>(tie);

        auto least = tie;
        auto const it = groups.find({key.first, 1U});
        if (it != end(groups) && it->second.least_ != nullptr) {
          ++n_pairs;
          auto const slot = walking(*it->second.least_);
          n_slot_vs_today += slot < today ? 1U : 0U;
          if (slot < tie) {
            ++n_slot_vs_tie;
            any = true;
            least = slot;
            auto const later = static_cast<int>(
                std::abs((it->second.least_->dest_time_ -
                          best.first_->dest_time_)
                             .count()));
            for (auto s = 0U; s != kSaved.size(); ++s) {
              for (auto l = 0U; l != kLater.size(); ++l) {
                if (tie - slot >= kSaved[s] && later <= kLater[l]) {
                  ++table[s][l];
                }
              }
            }
            if (later <= 15) {
              examples.emplace_back(tie - slot, best.least_,
                                    it->second.least_);
            }
          }
        }
        sum_any += static_cast<unsigned>(least);
      }
      n_queries += any ? 1U : 0U;
    }

    auto const avg = [&](unsigned const sum) {
      return n_best == 0U ? 0.0 : static_cast<double>(sum) / n_best;
    };
    fmt::println(
        "\n--- {} ---\n"
        "{:<60} {:>8}\n{:<60} {:>8}\n{:<60} {:>8}\n{:<60} {:>8}\n"
        "{:<60} {:>8}\n{:<60} {:>8}\n"
        "{:<60} {:>8.1f}\n{:<60} {:>8.1f}\n{:<60} {:>8.1f}",
        title,  //
        "results with best arrival", n_best,  //
        "  equal arrival with less walking (tie)", n_tie,  //
        "  have a journey of the extra slot", n_pairs,  //
        "    extra slot walks less than today's journey", n_slot_vs_today,
        "    extra slot walks less than tie", n_slot_vs_tie,  //
        "queries with such a journey", n_queries,  //
        "avg walking min, today", avg(sum_today),  //
        "avg walking min, tie", avg(sum_tie),  //
        "avg walking min, tie or extra slot", avg(sum_any));

    fmt::println(
        "\nextra slot walks less than tie: results by minutes saved and "
        "minutes later\n{:<16} {:>8} {:>8} {:>8} {:>8} {:>8}",
        "", "<= 5", "<= 15", "<= 30", "<= 60", "any");
    constexpr auto const kRows =
        std::array{">= 1 min", ">= 3 min", ">= 6 min", ">= 11 min"};
    for (auto s = 0U; s != kSaved.size(); ++s) {
      fmt::println("{:<16} {:>8} {:>8} {:>8} {:>8} {:>8}", kRows[s],
                   table[s][0], table[s][1], table[s][2], table[s][3],
                   table[s][4]);
    }

    utl::sort(examples, [](auto const& a, auto const& b) {
      return std::get<0>(a) > std::get<0>(b);
    });
    for (auto i = 0U; i < examples.size() && i / 10U < n_examples; i += 10U) {
      auto const& [d, best, slot] = examples[i];
      fmt::println("\n=== walks {} min less: best arrival ===", d);
      print_compact(tt, *best);
      fmt::println("--- extra slot ---");
      print_compact(tt, *slot);
    }
  };

  run(true, "walking between different stops: best arrival vs. extra slot");
  run(false, "walking incl. transfers at the same stop: best arrival vs. "
             "extra slot");
}

void print_memory_usage() {
#ifndef _WIN32
  auto r = rusage{};
  getrusage(RUSAGE_SELF, &r);
  std::cout << "\n--- memory usage ---\nrusage.ru_maxrss: "
            << static_cast<double>(r.ru_maxrss) / (1024 * 1024) << " GiB\n";
#endif
}

int main(int argc, char* argv[]) {
  setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);  // line buffering for CI

  namespace bpo = boost::program_options;

  auto tt_path = std::filesystem::path{};
  auto n_queries = std::uint32_t{100U};
  auto gs = query_generation::generator_settings{};
  auto interval_size = duration_t::rep{};
  auto bbox_str = std::string{};
  auto intermodal_start_str = std::string{};
  auto intermodal_dest_str = std::string{};
  auto max_transfers = std::uint32_t{kMaxTransfers};
  auto prf_idx = std::uint32_t{0};
  auto start_coord_str = std::string{};
  auto dest_coord_str = std::string{};
  auto start_loc_val = location_idx_t::value_t{0U};
  auto dest_loc_val = location_idx_t::value_t{0U};
  auto seed = std::int64_t{0};
  auto min_transfer_time = duration_t::rep{};
  auto qa_path = std::filesystem::path{};
  auto sb_csv_path = std::filesystem::path{};
  auto alt_csv_path = std::filesystem::path{};
  auto n_alternatives = std::uint32_t{0U};
  auto n_alt_examples = std::uint32_t{5U};
  auto n_reconstruct = std::uint32_t{1U};
  auto alt_tree = false;
  auto extra_slot = false;
  auto alt_compare = false;
  auto alt_query = std::int64_t{-1};
  auto criteria_csv_path = std::filesystem::path{};
  auto engines = std::vector<std::string>{"cpu", "gpu"};
  auto algos = std::vector<std::string>{"range", "pong"};
  auto modes = std::vector<std::string>{"s2s", "c2c"};
  auto dirs = std::vector<std::string>{"fwd"};
  auto threads_v =
      std::vector<unsigned>{std::max(std::thread::hardware_concurrency(), 1U)};
  auto gpu_states_v = std::vector<unsigned>{2U};

  bpo::options_description desc("Allowed options");
  desc.add_options()("help,h", "produce this help message")  //
      ("tt_path,p", bpo::value(&tt_path)->required(),
       "path to a binary file containing a serialized nigiri timetable")  //
      ("engines", bpo::value(&engines)->multitoken(),
       "engines to benchmark (default: cpu gpu); every axis is a vector -- "
       "the run is the full cross product of engines x algos x modes (x "
       "threads/states within an engine), all against the once-loaded "
       "timetable, with one PROFILE throughput/latency line per point; "
       "whenever BOTH engines ran a (mode, algo) cell, their pareto sets are "
       "cross-checked per query and the process exits non-zero on any "
       "divergence")  //
      ("algo,a", bpo::value(&algos)->multitoken(),
       "algorithms: raptor | pong (default: both); if both ran with the cpu "
       "engine, the pong cell of each (mode, dir) is checked against raptor "
       "for agreement on the intersection of the final search intervals")  //
      ("modes", bpo::value(&modes)->multitoken(),
       "<start>2<dest> query modes with s = station, c = coordinate: "
       "s2s | s2c | c2s | c2c (default: s2s c2c); c = intermodal offsets "
       "(walk)")  //
      ("dirs", bpo::value(&dirs)->multitoken(),
       "search directions: fwd | bwd (default: fwd); bwd flips the generated "
       "queries (start/dest swapped, vias reversed) and searches backward = "
       "arriveBy with the interval as arrival window; the interval extension "
       "flags are forced to the search direction (fwd: later only, bwd: "
       "earlier only), overriding -e/-l")  //
      ("threads", bpo::value(&threads_v)->multitoken(),
       "CPU worker thread counts to sweep (default: hardware "
       "concurrency)")  //
      ("gpu_states", bpo::value(&gpu_states_v)->multitoken(),
       "concurrent GPU pipeline counts to sweep (default: 2)")  //
      ("seed,s", bpo::value<std::int64_t>(&seed)->default_value(seed),
       "query generator RNG seed, -1 for a random seed")  //
      ("num_queries,n", bpo::value(&n_queries)->default_value(n_queries),
       "number of queries to generate/process")(
          "interval_size,i",
          bpo::value<duration_t::rep>(&interval_size)->default_value(60U, "60"),
          "the initial size of the search interval in minutes, set to 0 for "
          "ontrip queries")  //
      ("bounding_box,b", bpo::value<std::string>(&bbox_str),
       "limit randomized locations to a bounding box, "
       "format: lat_min,lon_min,lat_max,lon_max\ne.g., 36.0,-11.0,72.0,32.0\n"
       "(available via \"-b europe\")")  //
      ("intermodal_start",
       bpo::value<std::string>(&intermodal_start_str)->default_value("walk"),
       "first-mile transport mode for coordinate-* --modes: "
       "walk | bicycle | car")  //
      ("intermodal_dest",
       bpo::value<std::string>(&intermodal_dest_str)->default_value("walk"),
       "last-mile transport mode for *-coordinate --modes: "
       "walk | bicycle | car")  //
      ("use_start_footpaths",
       bpo::value<bool>(&gs.use_start_footpaths_)->default_value(true),
       "")  //
      ("max_transfers,t",
       bpo::value<std::uint32_t>(&max_transfers)->default_value(kMaxTransfers),
       "maximum number of transfers during routing")  //
      ("min_connection_count,m",
       bpo::value<std::uint32_t>(&gs.min_connection_count_)->default_value(5U),
       "the minimum number of connections to find with each query")  //
      ("extend_interval_earlier,e",
       bpo::value<bool>(&gs.extend_interval_earlier_)
           ->default_value(true, "true"),
       "allows extension of the search interval into the past")  //
      ("extend_interval_later,l",
       bpo::value<bool>(&gs.extend_interval_later_)
           ->default_value(true, "true"),
       "allows extension of the search interval into the future")  //
      ("profile_idx", bpo::value<std::uint32_t>(&prf_idx)->default_value(0U),
       "footpath profile index")  //
      ("allowed_claszes",
       bpo::value<clasz_mask_t>(&gs.allowed_claszes_)
           ->default_value(routing::all_clasz_allowed()),
       "")  //
      ("min_transfer_time",
       bpo::value<duration_t::rep>(&min_transfer_time)->default_value(0U),
       "minimum transfer time in minutes")  //
      ("transfer_time_factor",
       bpo::value<float>(&gs.transfer_time_settings_.factor_)
           ->default_value(1.0F),
       "multiply all transfer times by this factor")  //
      ("vias", bpo::value<unsigned>(&gs.n_vias_)->default_value(0U),
       "number of via stops")  //
      ("start_coord", bpo::value<std::string>(&start_coord_str),
       "start coordinate for random queries, format: \"(LAT, LON)\", "  //
       "where LAT/LON are given in decimal degrees")  //
      ("dest_coord", bpo::value<std::string>(&dest_coord_str),
       "destination coordinate for random queries, format: \"(LAT, LON)\", "  //
       "where LAT/LON are given in decimal degrees")  //
      ("start_loc", bpo::value<location_idx_t::value_t>(&start_loc_val),
       "start location for random queries")  //
      ("dest_loc", bpo::value<location_idx_t::value_t>(&dest_loc_val),
       "destination location for random queries")  //
      ("qa_path,q", bpo::value(&qa_path),
       "path to write the journey criteria to for qa")  //
      ("alternatives", bpo::value(&n_alternatives),
       "reference for alternatives: block routes / transfer stations of the "
       "fastest journey and search again, for the first n queries of each "
       "(mode, dir), with the first --threads value")  //
      ("n_alternatives", bpo::value(&n_reconstruct),
       "journeys to reconstruct for each result (query::n_alternatives_)")  //
      ("criteria_csv", bpo::value(&criteria_csv_path),
       "path to write the criteria of all journeys to (last cpu cell)")  //
      ("extra_slot", bpo::value(&extra_slot),
       "one extra arrival time for each label (query::extra_slot_), "
       "needs -i 0")  //
      ("alt_query", bpo::value(&alt_query),
       "with --alternatives: only this query")  //
      ("alt_compare", bpo::value(&alt_compare),
       "with --alternatives: compare blocking to the traversal of the extra "
       "slot tables")  //
      ("alt_tree", bpo::value(&alt_tree),
       "all combinations of the options of the legs "
       "(query::alternatives_tree_)")  //
      ("alt_csv", bpo::value(&alt_csv_path),
       "path to write the alternatives per query to")  //
      ("alt_examples", bpo::value(&n_alt_examples),
       "number of alternatives to print")  //
      ("sb_csv", bpo::value(&sb_csv_path),
       "path to write the second best counters per stop to (range algo, cpu)");
  bpo::variables_map vm;
  bpo::store(bpo::command_line_parser(argc, argv).options(desc).run(), vm);

  // process program options - begin
  if (vm.count("help") != 0U) {
    std::cout << desc << "\n";
    return 0;
  }

  bpo::notify(vm);

  std::cout << "loading timetable...\n";
  auto tt = *nigiri::timetable::read(tt_path);
  tt.resolve();

  gs.interval_size_ = duration_t{interval_size};

  if (!bbox_str.empty()) {
    gs.bbox_ = parse_bbox(bbox_str);
    if (!gs.bbox_.has_value()) {
      std::cout << "Error: malformed bounding box input\n";
      return 1;
    }
  }

  // transport modes of the first/last mile for coordinate-* / *-coordinate
  // --modes (the match modes themselves come from the mode tokens)
  auto const intermodal_start_mode =
      query_generation::to_transport_mode(intermodal_start_str);
  auto const intermodal_dest_mode =
      query_generation::to_transport_mode(intermodal_dest_str);
  if (!intermodal_start_mode || !intermodal_dest_mode) {
    std::cerr << "Error: unknown intermodal start/dest mode\n";
    return 1;
  }
  gs.start_mode_ = *intermodal_start_mode;
  gs.dest_mode_ = *intermodal_dest_mode;

  gs.max_transfers_ = max_transfers > std::numeric_limits<std::uint8_t>::max()
                          ? std::numeric_limits<std::uint8_t>::max()
                          : max_transfers;

  gs.transfer_time_settings_.min_transfer_time_ = duration_t{min_transfer_time};
  gs.transfer_time_settings_.default_ =
      min_transfer_time == 0U && gs.transfer_time_settings_.factor_ == 1.0F;

  if (vm.count("profile_idx") != 0) {
    if (prf_idx >= kNProfiles) {
      std::cout << "Error: profile idx exceeds numeric limits\n";
      return 1;
    }
    gs.prf_idx_ = prf_idx;
  }

  if (!start_coord_str.empty()) {
    gs.start_match_mode_ = location_match_mode::kIntermodal;
    auto const start_coord = parse_coord(start_coord_str);
    if (start_coord.has_value()) {
      gs.start_ = start_coord.value();
    } else {
      std::cout << "Error: Invalid start coordinate\n";
      return 1;
    }
  }

  if (!dest_coord_str.empty()) {
    gs.dest_match_mode_ = location_match_mode::kIntermodal;
    auto const dest_coord = parse_coord(dest_coord_str);
    if (dest_coord.has_value()) {
      gs.dest_ = dest_coord.value();
    } else {
      std::cout << "Error: Invalid destination coordinate\n";
      return 1;
    }
  }

  if (start_loc_val != 0U) {
    gs.start_match_mode_ = location_match_mode::kEquivalent;
    gs.start_ = location_idx_t{start_loc_val};
  }

  if (dest_loc_val != 0U) {
    gs.dest_match_mode_ = location_match_mode::kEquivalent;
    gs.dest_ = location_idx_t{dest_loc_val};
  }
  // process program options - end

  // ---- benchmark matrix: engines x algos x modes (x threads/states) ----
  for (auto const& d : dirs) {
    if (d != "fwd" && d != "bwd") {
      std::cerr << "invalid dir \"" << d << "\", expected fwd | bwd\n";
      return 1;
    }
  }

  auto run_cpu = false, run_gpu = false;
  for (auto const& e : engines) {
    if (e == "cpu") {
      run_cpu = true;
    } else if (e == "gpu") {
      run_gpu = true;
    } else {
      std::cerr << "invalid engine \"" << e << "\", expected cpu | gpu\n";
      return 1;
    }
  }
#if !defined(NIGIRI_CUDA)
  if (run_gpu) {
    if (!run_cpu) {
      std::cerr << "--engines gpu requires a NIGIRI_CUDA build\n";
      return 1;
    }
    std::cout << "NIGIRI_CUDA not enabled -> running CPU only\n";
    run_gpu = false;
  }
#endif
  for (auto const& a : algos) {
    if (a != "range" && a != "pong") {
      std::cerr << "invalid algo \"" << a << "\", expected raptor | pong\n";
      return 1;
    }
  }

  // apply one end of a <start>2<dest> mode token to the generator settings
  // (the first/last-mile transport modes come from --intermodal_start/_dest)
  auto const apply_mode = [](char const m, location_match_mode& match) {
    switch (m) {
      case 's': match = location_match_mode::kEquivalent; return true;
      case 'c': match = location_match_mode::kIntermodal; return true;
      default: return false;
    }
  };

  // padded markdown: renders as a table AND stays aligned as plain text;
  // one table for the whole matrix
  fmt::print("| {:<36} | {:>6} | {:>6} | {:>6} | {:>6} | {:>6} |\n",  //
             "config", "q/s", "avg ms", "median", "q90", "q99");
  fmt::print(
      "| {0:-<36} | {0:->5}: | {0:->5}: | {0:->5}: | {0:->5}: | "
      "{0:->5}: |\n",
      "");

  auto mode_queries =
      std::map<std::string,
               std::vector<nigiri::query_generation::start_dest_query>>{};
  auto summary = std::vector<std::string>{};
  auto sb_cells = std::vector<result_set>{};
  auto total = std::uint64_t{0U};
  auto qa_cell = std::optional<result_set>{};
  auto qa_n_cpu_cells = 0U;

#if defined(NIGIRI_CUDA)
  auto gpu_tt = std::optional<routing::gpu::gpu_timetable>{};
  if (run_gpu) {
    gpu_tt.emplace(tt);
  }
#endif

  for (auto const& mode : modes) {
    auto rs = gs;
    if (mode.size() != 3U || mode[1] != '2' ||
        !apply_mode(mode[0], rs.start_match_mode_) ||
        !apply_mode(mode[2], rs.dest_match_mode_)) {
      std::cerr << "invalid mode \"" << mode
                << "\", expected s2s | s2c | c2s | c2c\n";
      return 1;
    }
    if (rs.start_match_mode_ == location_match_mode::kIntermodal) {
      rs.use_start_footpaths_ = false;  // first mile is in the start offsets
    }

    auto& fwd_qs = mode_queries[mode];
    if (fwd_qs.empty()) {
      generate_queries(fwd_qs, n_queries, tt, rs, seed);
    }

    // (mode, dir) are the incomparable dimensions -- within one (mode, dir),
    // every (engine, algo) combination has to agree
    for (auto const& dir_str : dirs) {
      auto const dir =
          dir_str == "fwd" ? direction::kForward : direction::kBackward;

      auto qs = fwd_qs;
      for (auto& sdq : qs) {
        if (dir == direction::kBackward) {
          sdq.q_.flip_dir();
        }
        sdq.q_.extend_interval_earlier_ = dir == direction::kBackward;
        sdq.q_.extend_interval_later_ = dir == direction::kForward;
        sdq.q_.n_alternatives_ = static_cast<std::uint8_t>(n_reconstruct);
        sdq.q_.alternatives_tree_ = alt_tree;
        sdq.q_.extra_slot_ = extra_slot;
      }

      auto cells = std::vector<result_set>{};
      for (auto const& algo : algos) {
        auto const use_pong = algo == "pong";
        auto const label = mode + "-" + dir_str + "-" + algo;

        try {
          if (run_cpu) {
            cells.push_back(run_cell<cpu_ws>(
                qs, label + "-cpu", threads_v,
                [&](cpu_ws& w, routing::query q) {
                  auto const r =
                      use_pong
                          ? routing::pong_search(tt, nullptr, w.ss_, w.rs_,
                                                 std::move(q), dir)
                          : routing::raptor_search(tt, nullptr, w.ss_, w.rs_,
                                                   std::move(q), dir);
                  w.last_stats_ = r.algo_stats_;
                  return *r.journeys_;
                }));
            if (!use_pong) {
              sb_cells.push_back(cells.back());
            }
            if (!criteria_csv_path.empty()) {
              auto out = std::ofstream{criteria_csv_path};
              out << "query,start,dest,transfers,slot,alternative\n";
              for (auto const [i, set] : utl::enumerate(cells.back().res_)) {
                for (auto const& j : set) {
                  out << fmt::format(
                      "{},{},{},{},{},{}\n", i,
                      j.start_time_.time_since_epoch().count(),
                      j.dest_time_.time_since_epoch().count(), j.transfers_,
                      j.slot_, j.alternative_);
                }
              }
            }
            if (extra_slot && !use_pong) {
              print_walking_slot(cells.back().res_, tt, n_alt_examples);
              print_extra_slot(tt, cells.back(), n_alt_examples);
            }
            if (n_reconstruct > 1U) {
              auto n_results = 0U, n_with_alt = 0U, n_journeys = 0U;
              auto n_queries = 0U, n_fastest = 0U, n_errors = 0U;
              for (auto const& set : cells.back().res_) {
                auto any = false;
                auto fastest = static_cast<journey const*>(nullptr);
                for (auto it = begin(set); it != end(set); ++it) {
                  ++n_journeys;
                  n_errors += it->error_ || !it->is_reconstructed_ ? 1U : 0U;
                  if (it->alternative_ != 0U) {
                    continue;
                  }
                  ++n_results;
                  auto const has_alt = std::next(it) != end(set) &&
                                       std::next(it)->alternative_ != 0U;
                  n_with_alt += has_alt ? 1U : 0U;
                  any = any || has_alt;
                  if (fastest == nullptr ||
                      it->travel_time() < fastest->travel_time()) {
                    fastest = &*it;
                    n_fastest += 0U;
                  }
                }
                n_queries += any ? 1U : 0U;
                if (fastest != nullptr) {
                  auto const next = std::next(fastest);
                  n_fastest += next != &*end(set) && next->alternative_ != 0U
                                   ? 1U
                                   : 0U;
                }
              }
              print_walking(tt, cells.back().res_, n_alt_examples);
              fmt::println(
                  "\n--- reconstruction, n_alternatives={}: {} ---\n"
                  "{:<52} {:>8}\n{:<52} {:>8}\n{:<52} {:>8}\n"
                  "{:<52} {:>8}\n{:<52} {:>8}\n{:<52} {:>8}",
                  n_reconstruct, cells.back().label_,  //
                  "journeys", n_journeys,  //
                  "results (start time, dest time, transfers)", n_results,
                  "results with alternative", n_with_alt,  //
                  "queries with alternative", n_queries,  //
                  "queries, fastest journey has alternative", n_fastest,  //
                  "journeys not reconstructed", n_errors);
            }
            ++qa_n_cpu_cells;
            if (vm.count("qa_path")) {
              qa_cell = cells.back();
            }
          }

#if defined(NIGIRI_CUDA)
          if (run_gpu) {
            cells.push_back(run_cell<gpu_ws>(
                qs, label + "-gpu", gpu_states_v,
                [&](gpu_ws& w, routing::query q) {
                  auto const r =
                      use_pong
                          ? routing::pong_search(tt, nullptr, w.ss_, *w.rs_,
                                                 std::move(q), dir)
                          : routing::raptor_search(tt, nullptr, w.ss_, *w.rs_,
                                                   std::move(q), dir);
                  return *r.journeys_;
                },
                *gpu_tt));
          }
#endif
        } catch (std::exception const& e) {
          // e.g. GPU state allocation OOM -- report + fail instead of dying
          std::cerr << "RUN " << label << " failed: " << e.what() << "\n";
          summary.push_back(
              fmt::format("{:<24} EXCEPTION: {}", label, e.what()));
          ++total;
        }
      }

      if (n_alternatives != 0U) {
        auto alt_qs = qs;
        alt_qs.resize(std::min(alt_qs.size(), std::size_t{n_alternatives}));
        if (alt_query >= 0 &&
            static_cast<std::size_t>(alt_query) < alt_qs.size()) {
          alt_qs = {alt_qs[static_cast<std::size_t>(alt_query)]};
        }
        if (alt_compare) {
          compare_alternatives(tt, alt_qs, dir, threads_v.front(),
                               n_alt_examples);
        } else {
          run_alternatives(tt, alt_qs, dir, threads_v.front(), n_alt_examples,
                           alt_csv_path);
        }
      }

      if (cells.size() == 1U) {
        summary.push_back(fmt::format("{:<24} n={:<6} benchmark only",
                                      cells.front().label_, qs.size()));
      }
      for (auto a = std::size_t{0U}; a < cells.size(); ++a) {
        for (auto b = a + 1U; b < cells.size(); ++b) {
          auto const mismatches = compare_results(
              tt, cells[a].label_, cells[a].res_, cells[b].label_,
              cells[b].res_, qs, dir, gs.min_connection_count_);
          summary.push_back(
              fmt::format("{:<24} vs {:<24} n={:<6} mismatches={:<4} {}",
                          cells[a].label_, cells[b].label_, qs.size(),
                          mismatches, mismatches == 0U ? "PASS" : "FAIL"));
          total += mismatches;
        }
      }
    }
  }

  for (auto const& c : sb_cells) {
    print_sb_stats(tt, c, sb_cells.size() == 1U ? sb_csv_path : "");
  }

  std::cout << "\n=== SUMMARY ===\n";
  for (auto const& s : summary) {
    std::cout << s << "\n";
  }
  print_memory_usage();

  if (vm.count("qa_path")) {
    if (qa_n_cpu_cells != 1U || !qa_cell.has_value()) {
      std::cerr << "--qa_path requires exactly one cpu (mode, dir, algo) cell "
                   "(single-element --algo/--modes/--dirs)\n";
      return 1;
    }
    auto bm_crit = nigiri::qa::benchmark_criteria{};
    for (auto i = std::size_t{0U}; i != qa_cell->res_.size(); ++i) {
      auto jc = vector<nigiri::qa::criteria_t>{};
      for (auto const& j : qa_cell->res_[i]) {
        jc.emplace_back(
            static_cast<double>(j.start_time_.time_since_epoch().count()),
            static_cast<double>(j.dest_time_.time_since_epoch().count()),
            static_cast<double>(j.transfers_));
      }
      utl::sort(jc);
      auto const latency =
          i < qa_cell->latencies_.size() ? qa_cell->latencies_[i] : 0.0;
      bm_crit.qc_.emplace_back(
          i,
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::duration<double, std::milli>{
                  std::max(latency, 0.0)}),
          jc);
    }
    bm_crit.write(qa_path);
  }

  return total == 0U ? 0 : 1;
}
