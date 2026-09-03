// Compares the journeys the heuristic bidirectional lower-bound RAPTOR
// extracts (greedily, per transfer count) against the pareto front an actual
// RAPTOR computes for the same query.
//
// Writes queries.txt / responses.txt / metrics.csv / perk.csv / gaps.csv /
// summary.txt into --out_dir, so a run can be replayed (--queries) and the
// numbers can be aggregated outside.

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

#include "boost/program_options.hpp"

#include "utl/parser/cstr.h"

#include "geo/latlng.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/query_generator/generator.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/rt/frun.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace fs = std::filesystem;
namespace bpo = boost::program_options;
using namespace nigiri;
using namespace nigiri::routing;

namespace {

constexpr auto kNoGap = std::numeric_limits<int>::max();

struct bench_query {
  location_idx_t start_, dest_;
  unixtime_t start_time_;
};

struct jrny {
  unixtime_t start_, dep_, arr_;
  std::uint8_t transfers_;

  // Ontrip queries fix the departure, and the two algorithms report it
  // differently anyway (RAPTOR keeps the search anchor, lb tightens the access
  // leg), so journeys are compared on what they actually deliver: when they
  // arrive and how often the passenger changes.
  friend bool operator==(jrny const& a, jrny const& b) {
    return std::tie(a.arr_, a.transfers_) == std::tie(b.arr_, b.transfers_);
  }
  friend bool operator<(jrny const& a, jrny const& b) {
    return std::tie(a.arr_, a.transfers_) < std::tie(b.arr_, b.transfers_);
  }
};

// every location a journey touches, in travel order: the stops of each transit
// leg (not only where it is entered and left) plus the walk endpoints, mapped
// to their root so that platforms of one station count as one place
std::vector<location_idx_t> visited(timetable const& tt, journey const& j) {
  auto v = std::vector<location_idx_t>{};
  auto const add = [&](location_idx_t const l) {
    auto const c = tt.get_complex_idx(l);
    if (v.empty() || v.back() != c) {
      v.push_back(c);
    }
  };
  for (auto const& l : j.legs_) {
    if (auto const* const ree =
            std::get_if<journey::run_enter_exit>(&l.uses_)) {
      auto const t_idx = ree->r_.t_.t_idx_;
      if (t_idx == transport_idx_t::invalid()) {
        add(l.from_);
        add(l.to_);
        continue;
      }
      auto const& seq = tt.route_location_seq_[tt.transport_route_[t_idx]];
      for (auto i = ree->stop_range_.from_; i != ree->stop_range_.to_; ++i) {
        add(stop{seq[i]}.location_idx());
      }
    } else {
      add(l.from_);
      add(l.to_);
    }
  }
  return v;
}

// a location the journey comes back to after having left it
std::optional<location_idx_t> find_loop(timetable const& tt, journey const& j) {
  auto const v = visited(tt, j);
  auto seen = std::set<location_idx_t>{};
  for (auto const l : v) {
    if (!seen.insert(l).second) {
      return l;
    }
  }
  return std::nullopt;
}

jrny to_jrny(journey const& j) {
  // `start_time_` is the search anchor for RAPTOR's ontrip journeys but the
  // tightened departure for lb ones - the actual departure is what both agree
  // on, so that is what the comparison is keyed on.
  return {j.departure_time(), j.departure_time(), j.dest_time_,
          static_cast<std::uint8_t>(j.transfers_)};
}

// per-query result of one (raptor, lb) pair
struct cmp_result {
  std::size_t idx_;
  std::vector<jrny> r_, l_;  // raptor pareto front, lb alternatives (dedup)
  std::size_t l_raw_{0U};
  double r_ms_{0.0}, l_ms_{0.0};
  std::uint64_t patterns_{0U}, reconstructions_{0U}, truncated_{0U},
      unrealizable_{0U}, pruned_{0U}, repetitions_{0U}, passthrough_{0U},
      rounds_{0U}, round_ms_{0U};
  // per raptor journey: matched exactly? / arrival gap of the best lb
  // substitute (<= transfers, not departing earlier), kNoGap if none
  std::vector<std::uint8_t> matched_;
  std::vector<int> gap_;
  // --dump_legs: full itineraries, printed by journey::print
  std::string r_dump_, l_dump_;
};

std::string iso(unixtime_t const t) {
  return fmt::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(t));
}

int minutes(unixtime_t const a, unixtime_t const b) {
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::minutes>(a - b).count());
}

std::vector<jrny> dedup(std::vector<jrny> v) {
  std::sort(begin(v), end(v));
  v.erase(std::unique(begin(v), end(v)), end(v));
  return v;
}

// pareto front over (start [later better], arrival [earlier better],
// transfers [fewer better])
std::vector<jrny> pareto(std::vector<jrny> const& v) {
  auto out = std::vector<jrny>{};
  for (auto const& a : v) {
    auto const dominated = std::any_of(begin(v), end(v), [&](jrny const& b) {
      return b != a && b.start_ >= a.start_ && b.arr_ <= a.arr_ &&
             b.transfers_ <= a.transfers_;
    });
    if (!dominated) {
      out.push_back(a);
    }
  }
  return dedup(std::move(out));
}

void analyze(cmp_result& c) {
  c.matched_.resize(c.r_.size());
  c.gap_.resize(c.r_.size());
  for (auto i = std::size_t{0U}; i != c.r_.size(); ++i) {
    auto const& r = c.r_[i];
    c.matched_[i] = static_cast<std::uint8_t>(
        std::find(begin(c.l_), end(c.l_), r) != end(c.l_));
    auto best = kNoGap;
    for (auto const& l : c.l_) {
      if (l.transfers_ <= r.transfers_ && l.start_ >= r.start_) {
        best = std::min(best, minutes(l.arr_, r.arr_));
      }
    }
    c.gap_[i] = best;
  }
}

template <typename T>
T quantile(std::vector<T> v, double const q) {
  if (v.empty()) {
    return T{};
  }
  std::sort(begin(v), end(v));
  return v[std::min(v.size() - 1U, static_cast<std::size_t>(
                                       q * static_cast<double>(v.size())))];
}

}  // namespace

int main(int ac, char** av) {
  setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

  auto tt_path = fs::path{};
  auto out_dir = fs::path{"lb_compare"};
  auto queries_in = fs::path{};
  auto n_queries = std::uint32_t{100U};
  auto seed = std::int64_t{0};
  auto interval_size = duration_t::rep{0};
  auto max_patterns = 20U;
  auto max_transfers = std::uint32_t{kMaxTransfers};
  auto n_itineraries = 5U;
  auto n_threads = 4U;
  auto tag = std::string{};
  auto dump_legs = false;
  auto inspect = std::vector<location_idx_t::value_t>{};
  auto find_ids = std::vector<std::string>{};
  auto check_loops = false;
  auto lb_stats = false;

  auto desc = bpo::options_description{"Options"};
  desc.add_options()  //
      ("help,h", "produce this help message")  //
      ("tt_path,p", bpo::value(&tt_path)->required(),
       "serialized nigiri timetable (must be built with lb routes)")  //
      ("out_dir,o", bpo::value(&out_dir)->default_value(out_dir),
       "directory for queries.txt / responses.txt / *.csv / summary.txt")  //
      ("queries", bpo::value(&queries_in),
       "replay the queries of a previous run's queries.txt instead of "
       "generating new ones")  //
      ("num_queries,n", bpo::value(&n_queries)->default_value(n_queries))  //
      ("seed,s", bpo::value(&seed)->default_value(seed))  //
      ("interval_size,i",
       bpo::value(&interval_size)->default_value(interval_size),
       "search interval in minutes, 0 = ontrip (one departure time)")  //
      ("max_patterns", bpo::value(&max_patterns)->default_value(max_patterns),
       "bidir_lb_raptor::max_patterns_ (global budget per query)")  //
      ("max_transfers,t",
       bpo::value(&max_transfers)->default_value(max_transfers))  //
      ("num_itineraries",
       bpo::value(&n_itineraries)->default_value(n_itineraries),
       "journeys the lb search has to reach (query.min_connection_count_); the "
       "search window does not bound it")  //
      ("threads", bpo::value(&n_threads)->default_value(n_threads))  //
      ("tag", bpo::value(&tag)->default_value(tag),
       "suffix for the output file names")  //
      ("lb_stats", bpo::bool_switch(&lb_stats)->default_value(false),
       "what the root-collapsing in build_lb_routes buys: route merge factor "
       "and scan size, with and without it")  //
      ("loops", bpo::bool_switch(&check_loops)->default_value(false),
       "report journeys that visit a location twice (all stops of every "
       "transit leg included), for both algorithms")  //
      ("find", bpo::value(&find_ids)->multitoken(),
       "resolve these location ids to location indices, then exit")  //
      ("inspect", bpo::value(&inspect)->multitoken(),
       "print the kEquivalent expansion (and the root each entry maps to) of "
       "these location indices, then exit")  //
      ("dump_legs", bpo::bool_switch(&dump_legs)->default_value(false),
       "write the full itineraries of both algorithms into responses.txt "
       "(only useful for a handful of queries)");

  auto vm = bpo::variables_map{};
  bpo::store(bpo::command_line_parser(ac, av).options(desc).run(), vm);
  if (vm.count("help") != 0U) {
    std::cout << desc << "\n";
    return 0;
  }
  bpo::notify(vm);

  fs::create_directories(out_dir);
  auto const suffix = tag.empty() ? std::string{} : "_" + tag;

  std::cout << "loading timetable " << tt_path << "...\n";
  auto tt = *timetable::read(tt_path);
  tt.resolve();
  if (tt.n_lb_routes(profile_idx_t{0}) == 0U) {
    std::cerr << "ERROR: timetable has no lb routes -- re-import with this "
                 "branch\n";
    return 1;
  }
  std::cout << "locations=" << tt.n_locations() << " routes=" << tt.n_routes()
            << " lb_routes=" << tt.n_lb_routes(profile_idx_t{0}) << "\n";

  if (lb_stats) {
    auto const prf = profile_idx_t{0};

    auto n_roots = 0U, n_children = 0U;
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      if (tt.locations_.parents_[l] == location_idx_t::invalid()) {
        ++n_roots;
      } else {
        ++n_children;
      }
    }

    // how the loader merges today: routes with an identical *root* sequence
    auto root_seqs = std::set<std::vector<location_idx_t>>{};
    auto raw_seqs = std::set<std::vector<location_idx_t>>{};
    auto n_stops = std::size_t{0U}, n_non_root_stops = std::size_t{0U};
    for (auto const r : interval{route_idx_t{0U}, route_idx_t{tt.n_routes()}}) {
      auto root = std::vector<location_idx_t>{};
      auto raw = std::vector<location_idx_t>{};
      for (auto const s : tt.route_location_seq_[r]) {
        auto const l = stop{s}.location_idx();
        raw.push_back(l);
        root.push_back(tt.locations_.get_root_idx(l));
        ++n_stops;
        if (tt.locations_.get_root_idx(l) != l) {
          ++n_non_root_stops;
        }
      }
      root_seqs.insert(root);
      raw_seqs.insert(raw);
    }

    auto lb_stops = std::size_t{0U};
    for (auto const& seq : tt.lb_route_complex_seq_[prf]) {
      lb_stops += seq.size();
    }
    auto n_with_routes = 0U;
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      if (!tt.location_lb_routes_[prf][l].empty()) {
        ++n_with_routes;
      }
    }

    fmt::println("locations {} (roots {}, children {})", tt.n_locations(),
                 n_roots, n_children);
    fmt::println("routes {}", tt.n_routes());
    fmt::println("  merged by root sequence (what is built): {}",
                 root_seqs.size());
    fmt::println("  merged by raw  sequence (no collapsing): {}",
                 raw_seqs.size());
    fmt::println("  -> collapsing merges {} routes more ({:.1f}% fewer)",
                 raw_seqs.size() - root_seqs.size(),
                 100.0 *
                     static_cast<double>(raw_seqs.size() - root_seqs.size()) /
                     static_cast<double>(raw_seqs.size()));
    fmt::println("route stops {} of which on a child location {} ({:.1f}%)",
                 n_stops, n_non_root_stops,
                 100.0 * static_cast<double>(n_non_root_stops) /
                     static_cast<double>(n_stops));
    fmt::println("lb routes {} with {} stops in total", tt.n_lb_routes(prf),
                 lb_stops);
    fmt::println("locations carrying lb routes (what a round scans): {}",
                 n_with_routes);

    // what a *station complex* mapping would give instead: union-find over
    // parent/child *and* equivalences, i.e. what one might assume "lb routes
    // over equivalences" means
    auto uf = std::vector<location_idx_t::value_t>(tt.n_locations());
    std::iota(begin(uf), end(uf), 0U);
    auto find = [&](location_idx_t::value_t x) {
      while (uf[x] != x) {
        uf[x] = uf[uf[x]];
        x = uf[x];
      }
      return x;
    };
    auto unite = [&](location_idx_t const a, location_idx_t const b) {
      auto const ra = find(to_idx(a)), rb = find(to_idx(b));
      if (ra != rb) {
        uf[std::max(ra, rb)] = std::min(ra, rb);
      }
    };
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      if (tt.locations_.parents_[l] != location_idx_t::invalid()) {
        unite(l, tt.locations_.parents_[l]);
      }
      for (auto const eq : tt.locations_.equivalences_[l]) {
        unite(l, eq);
      }
    }
    auto complex_seqs = std::set<std::vector<location_idx_t::value_t>>{};
    for (auto const r : interval{route_idx_t{0U}, route_idx_t{tt.n_routes()}}) {
      auto seq = std::vector<location_idx_t::value_t>{};
      for (auto const s : tt.route_location_seq_[r]) {
        seq.push_back(find(to_idx(stop{s}.location_idx())));
      }
      // a complex mapping also collapses consecutive stops in one complex
      seq.erase(std::unique(begin(seq), end(seq)), end(seq));
      complex_seqs.insert(seq);
    }
    auto complexes = std::set<location_idx_t::value_t>{};
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      complexes.insert(find(to_idx(l)));
    }
    // how far the union-find drifts: equivalence is *not* transitive in the
    // timetable (A~B, B~C, but not A~C), a complex makes it transitive
    auto members =
        std::map<location_idx_t::value_t, std::vector<location_idx_t>>{};
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      members[find(to_idx(l))].push_back(l);
    }
    auto sizes = std::vector<std::size_t>{};
    auto max_diameter = 0.0;
    auto worst = location_idx_t::invalid();
    for (auto const& [c, ls] : members) {
      sizes.push_back(ls.size());
      if (ls.size() < 2U || ls.size() > 200U) {
        continue;  // the huge ones are measured below, pairwise is too slow
      }
      for (auto const a : ls) {
        for (auto const b : ls) {
          auto const d = geo::distance(tt.locations_.coordinates_[a],
                                       tt.locations_.coordinates_[b]);
          if (d > max_diameter) {
            max_diameter = d;
            worst = a;
          }
        }
      }
    }
    utl::sort(sizes);
    fmt::println("");
    fmt::println("station complexes (parent/child + equivalences): {}",
                 complexes.size());
    fmt::println("  members per complex: p50 {} p99 {} max {}",
                 sizes[sizes.size() / 2U], sizes[sizes.size() * 99U / 100U],
                 sizes.back());
    fmt::println(
        "  largest diameter among complexes <= 200 members: {:.0f} m "
        "(e.g. {})",
        max_diameter,
        worst == location_idx_t::invalid()
            ? std::string{"-"}
            : std::string{tt.get_default_name(worst)});
    fmt::println(
        "  routes merged by complex sequence: {} ({:.1f}% fewer than "
        "by root)",
        complex_seqs.size(),
        100.0 * static_cast<double>(root_seqs.size() - complex_seqs.size()) /
            static_cast<double>(root_seqs.size()));
    return 0;
  }

  if (!find_ids.empty()) {
    for (auto const l :
         interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
      auto const id = tt.locations_.ids_[l].view();
      for (auto const& needle : find_ids) {
        if (id == needle) {
          std::cout << to_idx(l) << " " << tt.get_default_name(l) << " (" << id
                    << ") complex=" << to_idx(tt.get_complex_idx(l)) << "\n";
        }
      }
    }
    return 0;
  }

  if (!inspect.empty()) {
    for (auto const v : inspect) {
      auto const l = location_idx_t{v};
      std::cout << "\nlocation " << v << " " << tt.get_default_name(l) << " ("
                << tt.locations_.ids_[l].view()
                << ") complex=" << to_idx(tt.get_complex_idx(l)) << "\n";
      for (auto const mode : {location_match_mode::kEquivalent,
                              location_match_mode::kIntermodal}) {
        std::cout << "  for_each_meta("
                  << (mode == location_match_mode::kEquivalent ? "kEquivalent"
                                                               : "kIntermodal")
                  << "):\n";
        for_each_meta(tt, mode, l, [&](location_idx_t const m) {
          auto const cplx = tt.get_complex_idx(m);
          // can `realize` actually walk from `l` to `m`? only footpaths count
          auto fp = std::optional<duration_t>{};
          for (auto const& f :
               tt.locations_.footpaths_out_[profile_idx_t{0}][l]) {
            if (f.target() == m) {
              fp = f.duration();
            }
          }
          std::cout << "    " << to_idx(m) << " " << tt.get_default_name(m)
                    << " (" << tt.locations_.ids_[m].view() << ") -> complex "
                    << to_idx(cplx) << " " << tt.get_default_name(cplx)
                    << "  footpath from " << to_idx(l) << ": "
                    << (m == l           ? "self"
                        : fp.has_value() ? fmt::format("{}", *fp)
                                         : "NONE")
                    << "\n";
        });
      }
    }
    return 0;
  }

  // ---- queries ----
  auto qs = std::vector<bench_query>{};
  if (!queries_in.empty()) {
    auto in = std::ifstream{queries_in};
    auto line = std::string{};
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      auto ss = std::stringstream{line};
      auto idx = std::string{}, s = std::string{}, d = std::string{},
           t = std::string{};
      std::getline(ss, idx, '\t');
      std::getline(ss, s, '\t');
      std::getline(ss, d, '\t');
      std::getline(ss, t, '\t');
      qs.push_back(
          {location_idx_t{static_cast<location_idx_t::value_t>(std::stoul(s))},
           location_idx_t{static_cast<location_idx_t::value_t>(std::stoul(d))},
           unixtime_t{i32_minutes{static_cast<std::int32_t>(std::stoll(t))}}});
    }
    std::cout << "replaying " << qs.size() << " queries from " << queries_in
              << "\n";
  } else {
    auto gs = query_generation::generator_settings{};
    gs.interval_size_ = duration_t{interval_size};
    gs.start_match_mode_ = location_match_mode::kEquivalent;
    gs.dest_match_mode_ = location_match_mode::kEquivalent;
    gs.use_start_footpaths_ = true;
    gs.min_connection_count_ = 0U;
    gs.extend_interval_earlier_ = false;
    gs.extend_interval_later_ = false;
    auto qg =
        seed > -1
            ? query_generation::generator{tt, gs,
                                          static_cast<std::uint32_t>(seed)}
            : query_generation::generator{tt, gs};
    while (qs.size() != n_queries) {
      auto const sdq = qg.random_query();
      if (!sdq.has_value()) {
        continue;
      }
      auto const& q = sdq->q_;
      if (q.start_.empty() || q.destination_.empty()) {
        continue;
      }
      auto const t = std::visit(
          utl::overloaded{[](unixtime_t const x) { return x; },
                          [](interval<unixtime_t> const i) { return i.from_; }},
          q.start_time_);
      qs.push_back(
          {q.start_.front().target(), q.destination_.front().target(), t});
    }
    std::cout << "generated " << qs.size() << " queries (seed=" << seed
              << ")\n";
  }

  // a query outside the timetable would silently produce empty results
  auto const tt_itv = tt.external_interval();
  for (auto const& q : qs) {
    if (!tt_itv.contains(q.start_time_)) {
      std::cerr << "ERROR: query time " << iso(q.start_time_)
                << " is outside the timetable " << iso(tt_itv.from_) << " - "
                << iso(tt_itv.to_) << "\n";
      return 1;
    }
  }

  {
    auto out = std::ofstream{out_dir / ("queries" + suffix + ".txt")};
    out << "# idx\tstart_idx\tdest_idx\tstart_time_unix_min\tstart_name\t"
           "dest_name\tstart_time_iso\tinterval_min="
        << interval_size << "\n";
    for (auto const [i, q] : utl::enumerate(qs)) {
      out << i << '\t' << to_idx(q.start_) << '\t' << to_idx(q.dest_) << '\t'
          << q.start_time_.time_since_epoch().count() << '\t'
          << tt.get_default_name(q.start_) << '\t'
          << tt.get_default_name(q.dest_) << '\t' << iso(q.start_time_) << '\n';
    }
  }

  // ---- build the routing query for one bench query ----
  auto const make_q = [&](bench_query const& bq) {
    auto q = query{};
    if (interval_size == 0) {
      q.start_time_ = bq.start_time_;
    } else {
      q.start_time_ = interval<unixtime_t>{
          bq.start_time_, bq.start_time_ + duration_t{interval_size}};
    }
    q.start_match_mode_ = location_match_mode::kEquivalent;
    q.dest_match_mode_ = location_match_mode::kEquivalent;
    q.use_start_footpaths_ = true;
    q.start_ = {{bq.start_, 0_minutes, 0U}};
    q.destination_ = {{bq.dest_, 0_minutes, 0U}};
    q.max_transfers_ = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(max_transfers, kMaxTransfers));
    // no interval extension: both algorithms have to answer the *same*
    // interval, otherwise the comparison is meaningless
    q.min_connection_count_ = n_itineraries;
    q.extend_interval_earlier_ = false;
    q.extend_interval_later_ = false;
    return q;
  };

  if (check_loops) {
    auto ss = search_state{};
    auto rs = raptor_state{};
    auto lbr = bidir_lb_raptor{};
    lbr.max_patterns_ = max_patterns;
    auto n_lb = 0U, n_lb_loops = 0U, n_r = 0U, n_r_loops = 0U;
    auto examples = 0U;
    auto by_k = std::map<unsigned, std::pair<unsigned, unsigned>>{};
    auto n_match = 0U, n_match_loops = 0U;
    for (auto const [i, bq] : utl::enumerate(qs)) {
      auto q = make_q(bq);
      q.sanitize(tt);
      lbr.execute(tt, nullptr, q, false);
      auto q2 = make_q(bq);
      auto const r =
          raptor_search(tt, nullptr, ss, rs, q2, direction::kForward);
      auto front = std::vector<jrny>{};
      for (auto const& j : *r.journeys_) {
        ++n_r;
        front.push_back(to_jrny(j));
        if (find_loop(tt, j).has_value()) {
          ++n_r_loops;
        }
      }

      for (auto const& j : lbr.journeys_) {
        ++n_lb;
        auto& b = by_k[j.transfers_];
        ++b.first;
        auto const on_front =
            std::find(begin(front), end(front), to_jrny(j)) != end(front);
        if (on_front) {
          ++n_match;
        }
        if (auto const l = find_loop(tt, j); l.has_value()) {
          ++n_lb_loops;
          ++b.second;
          if (on_front) {
            ++n_match_loops;
          }
          if (examples < 2U) {
            ++examples;
            std::cout << "\n--- lb journey revisiting "
                      << tt.get_default_name(*l) << " (query " << i << ")\n";
            j.print(std::cout, tt, nullptr);
          }
        }
      }
    }
    fmt::println("\nloops: lb {}/{} journeys ({:.1f}%), raptor {}/{} ({:.1f}%)",
                 n_lb_loops, n_lb, n_lb == 0U ? 0.0 : 100.0 * n_lb_loops / n_lb,
                 n_r_loops, n_r, n_r == 0U ? 0.0 : 100.0 * n_r_loops / n_r);
    fmt::println(
        "lb journeys that are on the raptor front: {}/{}, of those {} "
        "({:.1f}%) contain a loop",
        n_match, n_lb, n_match_loops,
        n_match == 0U ? 0.0 : 100.0 * n_match_loops / n_match);
    fmt::println("by transfer count (journeys / with loop):");
    for (auto const& [k, v] : by_k) {
      fmt::println("  {:>2}: {:>7} / {:>7} ({:.1f}%)", k, v.first, v.second,
                   v.first == 0U ? 0.0 : 100.0 * v.second / v.first);
    }
    return 0;
  }

  // ---- run ----
  auto results = std::vector<cmp_result>(qs.size());
  auto next = std::atomic<std::size_t>{0U};
  auto failed = std::atomic<std::size_t>{0U};
  auto done = std::atomic<std::size_t>{0U};
  auto workers = std::vector<std::thread>{};
  auto const t_start = std::chrono::steady_clock::now();
  for (auto w = 0U; w != std::max(1U, n_threads); ++w) {
    workers.emplace_back([&]() {
      auto ss = search_state{};
      auto rs = raptor_state{};
      auto lbr = bidir_lb_raptor{};
      lbr.max_patterns_ = max_patterns;
      for (auto i = next.fetch_add(1U); i < qs.size(); i = next.fetch_add(1U)) {
        auto& c = results[i];
        c.idx_ = i;
        try {
          {
            auto q = make_q(qs[i]);
            auto const t0 = std::chrono::steady_clock::now();
            auto const r =
                raptor_search(tt, nullptr, ss, rs, q, direction::kForward);
            c.r_ms_ = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
            for (auto const& j : *r.journeys_) {
              c.r_.push_back(to_jrny(j));
            }
            if (dump_legs) {
              auto ss = std::stringstream{};
              for (auto const& j : *r.journeys_) {
                j.print(ss, tt, nullptr);
                ss << "\n";
              }
              c.r_dump_ = ss.str();
            }
          }
          {
            auto q = make_q(qs[i]);
            q.sanitize(tt);
            auto const t0 = std::chrono::steady_clock::now();
            lbr.execute(tt, nullptr, q, false);
            c.l_ms_ = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
            c.l_raw_ = lbr.journeys_.size();
            for (auto const& j : lbr.journeys_) {
              c.l_.push_back(to_jrny(j));
            }
            if (dump_legs) {
              auto ss = std::stringstream{};
              for (auto const& j : lbr.journeys_) {
                j.print(ss, tt, nullptr);
                ss << "\n";
              }
              c.l_dump_ = ss.str();
            }
            c.patterns_ = lbr.patterns_.size();
            c.reconstructions_ = lbr.stats_.pattern_reconstructions_;
            c.truncated_ = lbr.stats_.truncated_patterns_;
            c.unrealizable_ = lbr.stats_.unrealizable_patterns_;
            c.pruned_ = lbr.stats_.pruned_meetpoints_;
            c.repetitions_ = lbr.stats_.pattern_repetitions_;
            c.passthrough_ = lbr.stats_.passthrough_patterns_;
            c.rounds_ = lbr.stats_.rounds_;
            c.round_ms_ = lbr.stats_.round_ms_;
          }
          c.r_ = dedup(std::move(c.r_));
          c.l_ = dedup(std::move(c.l_));
          analyze(c);
        } catch (std::exception const& e) {
          std::cerr << "q#" << i << " FAILED: " << e.what() << "\n";
          failed.fetch_add(1U);
        }
        auto const d = done.fetch_add(1U) + 1U;
        if (d % 100U == 0U) {
          std::cout << d << "/" << qs.size() << "\n";
        }
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }
  auto const wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t_start)
                           .count();

  // ---- responses ----
  {
    auto out = std::ofstream{out_dir / ("responses" + suffix + ".txt")};
    for (auto const& c : results) {
      out << "=== query " << c.idx_ << " "
          << tt.get_default_name(qs[c.idx_].start_) << " -> "
          << tt.get_default_name(qs[c.idx_].dest_) << " @ "
          << iso(qs[c.idx_].start_time_) << " raptor_ms=" << c.r_ms_
          << " lb_ms=" << c.l_ms_ << " patterns=" << c.patterns_
          << " lb_journeys_raw=" << c.l_raw_ << "\n";
      for (auto const [i, r] : utl::enumerate(c.r_)) {
        out << "R\tdep=" << iso(r.dep_) << "\tarr=" << iso(r.arr_)
            << "\ttransfers=" << unsigned{r.transfers_}
            << "\ttravel_min=" << minutes(r.arr_, r.dep_)
            << "\tmatched=" << unsigned{c.matched_[i]} << "\tgap_min="
            << (c.gap_[i] == kNoGap ? std::string{"none"}
                                    : std::to_string(c.gap_[i]))
            << "\n";
      }
      if (!c.r_dump_.empty()) {
        out << "--- raptor itineraries ---\n" << c.r_dump_;
      }
      for (auto const& l : c.l_) {
        out << "L\tdep=" << iso(l.dep_) << "\tarr=" << iso(l.arr_)
            << "\ttransfers=" << unsigned{l.transfers_}
            << "\ttravel_min=" << minutes(l.arr_, l.dep_) << "\texact="
            << (std::find(begin(c.r_), end(c.r_), l) != end(c.r_) ? 1 : 0)
            << "\n";
      }
      if (!c.l_dump_.empty()) {
        out << "--- lb itineraries ---\n" << c.l_dump_;
      }
    }
  }

  // ---- metrics.csv (one row per query) ----
  {
    auto out = std::ofstream{out_dir / ("metrics" + suffix + ".csv")};
    out << "query,n_raptor,n_lb,n_lb_raw,n_lb_pareto,exact_hits,recall,"
           "n_missed,max_gap_min,mean_gap_min,n_no_sub,raptor_ms,lb_ms,"
           "patterns,reconstructions,truncated,unrealizable,pruned,"
           "repetitions,passthrough,rounds,round_ms,min_transfers_raptor,"
           "min_transfers_lb\n";
    for (auto const& c : results) {
      auto const hits = static_cast<std::size_t>(
          std::count(begin(c.matched_), end(c.matched_), std::uint8_t{1}));
      auto gaps = std::vector<int>{};
      auto no_sub = std::size_t{0U};
      for (auto const g : c.gap_) {
        if (g == kNoGap) {
          ++no_sub;
        } else if (g > 0) {
          gaps.push_back(g);
        }
      }
      auto const max_gap =
          gaps.empty() ? 0 : *std::max_element(begin(gaps), end(gaps));
      auto const mean_gap = gaps.empty()
                                ? 0.0
                                : std::accumulate(begin(gaps), end(gaps), 0.0) /
                                      static_cast<double>(gaps.size());
      auto const min_tr = [](std::vector<jrny> const& v) {
        auto m = 255;
        for (auto const& j : v) {
          m = std::min(m, int{j.transfers_});
        }
        return m == 255 ? -1 : m;
      };
      out << c.idx_ << ',' << c.r_.size() << ',' << c.l_.size() << ','
          << c.l_raw_ << ',' << pareto(c.l_).size() << ',' << hits << ','
          << (c.r_.empty() ? 0.0
                           : static_cast<double>(hits) /
                                 static_cast<double>(c.r_.size()))
          << ',' << (c.r_.size() - hits) << ',' << max_gap << ',' << mean_gap
          << ',' << no_sub << ',' << c.r_ms_ << ',' << c.l_ms_ << ','
          << c.patterns_ << ',' << c.reconstructions_ << ',' << c.truncated_
          << ',' << c.unrealizable_ << ',' << c.pruned_ << ',' << c.repetitions_
          << ',' << c.passthrough_ << ',' << c.rounds_ << ',' << c.round_ms_
          << ',' << min_tr(c.r_) << ',' << min_tr(c.l_) << '\n';
    }
  }

  // ---- gaps.csv (one row per raptor pareto journey) ----
  {
    auto out = std::ofstream{out_dir / ("gaps" + suffix + ".csv")};
    out << "query,transfers,dep_iso,arr_iso,travel_min,matched,gap_min\n";
    for (auto const& c : results) {
      for (auto const [i, r] : utl::enumerate(c.r_)) {
        out << c.idx_ << ',' << unsigned{r.transfers_} << ',' << iso(r.dep_)
            << ',' << iso(r.arr_) << ',' << minutes(r.arr_, r.dep_) << ','
            << unsigned{c.matched_[i]} << ',' << c.gap_[i] << '\n';
      }
    }
  }

  // ---- perk.csv: best arrival with at most k transfers ----
  {
    auto out = std::ofstream{out_dir / ("perk" + suffix + ".csv")};
    out << "query,k,raptor_arr_iso,lb_arr_iso,gap_min\n";
    for (auto const& c : results) {
      if (c.r_.empty()) {
        continue;
      }
      auto k_max = 0;
      for (auto const& r : c.r_) {
        k_max = std::max(k_max, int{r.transfers_});
      }
      auto const best = [](std::vector<jrny> const& v, int const k) {
        auto b = std::optional<unixtime_t>{};
        for (auto const& j : v) {
          if (int{j.transfers_} <= k && (!b || j.arr_ < *b)) {
            b = j.arr_;
          }
        }
        return b;
      };
      for (auto k = 0; k <= k_max; ++k) {
        auto const rb = best(c.r_, k);
        auto const lb = best(c.l_, k);
        if (!rb.has_value()) {
          continue;
        }
        out << c.idx_ << ',' << k << ',' << iso(*rb) << ','
            << (lb ? iso(*lb) : std::string{"none"}) << ','
            << (lb ? std::to_string(minutes(*lb, *rb)) : std::string{"none"})
            << '\n';
      }
    }
  }

  // ---- summary ----
  auto ss = std::stringstream{};
  auto n_r = std::size_t{0U}, n_l = std::size_t{0U}, n_hits = std::size_t{0U},
       n_no_sub = std::size_t{0U}, n_empty_r = std::size_t{0U},
       n_empty_l = std::size_t{0U};
  auto per_query_recall = std::vector<double>{};
  auto all_gaps = std::vector<int>{};
  auto r_ms = std::vector<double>{}, l_ms = std::vector<double>{};
  auto perfect = std::size_t{0U};
  for (auto const& c : results) {
    n_r += c.r_.size();
    n_l += c.l_.size();
    auto const hits = static_cast<std::size_t>(
        std::count(begin(c.matched_), end(c.matched_), std::uint8_t{1}));
    n_hits += hits;
    if (c.r_.empty()) {
      ++n_empty_r;
    } else {
      per_query_recall.push_back(static_cast<double>(hits) /
                                 static_cast<double>(c.r_.size()));
      if (hits == c.r_.size()) {
        ++perfect;
      }
    }
    if (c.l_.empty()) {
      ++n_empty_l;
    }
    for (auto const g : c.gap_) {
      if (g == kNoGap) {
        ++n_no_sub;
      } else {
        all_gaps.push_back(std::max(0, g));
      }
    }
    r_ms.push_back(c.r_ms_);
    l_ms.push_back(c.l_ms_);
  }
  auto const mean = [](auto const& v) {
    return v.empty() ? 0.0
                     : std::accumulate(begin(v), end(v), 0.0) /
                           static_cast<double>(v.size());
  };
  ss << "queries=" << qs.size() << " failed=" << failed.load()
     << " interval_min=" << interval_size << " max_patterns=" << max_patterns
     << " wall_ms=" << wall_ms << "\n"
     << "raptor journeys (pareto)      : " << n_r
     << " (empty results: " << n_empty_r << ")\n"
     << "lb journeys (distinct)        : " << n_l
     << " (empty results: " << n_empty_l << ")\n"
     << "exact hits                    : " << n_hits << " ("
     << (n_r == 0
             ? 0.0
             : 100.0 * static_cast<double>(n_hits) / static_cast<double>(n_r))
     << "% of the pareto front)\n"
     << "queries with full pareto front: " << perfect << "/"
     << per_query_recall.size() << "\n"
     << "mean per-query recall         : " << mean(per_query_recall) << "\n"
     << "pareto journeys w/o any lb substitute (<= transfers, not earlier): "
     << n_no_sub << "\n"
     << "arrival gap of the best lb substitute [min]: mean=" << mean(all_gaps)
     << " p50=" << quantile(all_gaps, 0.5) << " p90=" << quantile(all_gaps, 0.9)
     << " p99=" << quantile(all_gaps, 0.99) << " max="
     << (all_gaps.empty() ? 0
                          : *std::max_element(begin(all_gaps), end(all_gaps)))
     << "\n"
     << "runtime [ms]: raptor mean=" << mean(r_ms)
     << " p50=" << quantile(r_ms, 0.5) << " p90=" << quantile(r_ms, 0.9)
     << " | lb mean=" << mean(l_ms) << " p50=" << quantile(l_ms, 0.5)
     << " p90=" << quantile(l_ms, 0.9) << "\n";

  std::cout << "\n=== SUMMARY ===\n" << ss.str();
  auto out = std::ofstream{out_dir / ("summary" + suffix + ".txt")};
  out << ss.str();

  return 0;
}
