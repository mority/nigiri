#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "boost/program_options.hpp"

#include "fmt/format.h"

#include "date/date.h"

#include "utl/verify.h"

#include "nigiri/routing/one_to_n.h"
#include "nigiri/routing/one_to_n_accumulators.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"

namespace bpo = boost::program_options;
using namespace nigiri;
using namespace nigiri::routing;

namespace {

std::optional<geo::box> parse_bbox(std::string const& str) {
  auto v = std::vector<double>{};
  auto pos = std::size_t{0U};
  while (pos <= str.size()) {
    auto const next = std::min(str.find(',', pos), str.size());
    try {
      v.push_back(std::stod(str.substr(pos, next - pos)));
    } catch (...) {
      return std::nullopt;
    }
    pos = next + 1U;
  }
  if (v.size() != 4U) {
    return std::nullopt;
  }
  return geo::box{geo::latlng{v[0], v[1]}, geo::latlng{v[2], v[3]}};
}

std::optional<unixtime_t> parse_time(std::string const& str) {
  auto in = std::istringstream{str};
  auto t = date::sys_time<std::chrono::minutes>{};
  in >> date::parse("%FT%R", t);
  if (in.fail()) {
    return std::nullopt;
  }
  return std::chrono::time_point_cast<i32_minutes>(t);
}

struct null_accumulator final : public one_to_n_accumulator {
  std::unique_ptr<one_to_n_accumulator> clone() const override {
    return std::make_unique<null_accumulator>();
  }
  void begin_origin(cell_idx_t) override {}
  void add_sample(unixtime_t, one_to_n_sample const&) override {}
  void end_origin(cell_idx_t) override {}
};

struct collector final : public one_to_n_accumulator {
  explicit collector(std::map<cell_idx_t, std::vector<one_to_n_label>>& m)
      : m_{m} {}
  std::unique_ptr<one_to_n_accumulator> clone() const override {
    return std::make_unique<collector>(m_);
  }
  void begin_origin(cell_idx_t) override {}
  void add_sample(unixtime_t, one_to_n_sample const& s) override {
    for (auto const c : s.reached()) {
      auto const labels = s[c];
      m_[c] = {begin(labels), end(labels)};
    }
  }
  void end_origin(cell_idx_t) override {}
  std::map<cell_idx_t, std::vector<one_to_n_label>>& m_;
};

// Pareto set of (k, travel time) from the intermodal point-to-point search.
std::vector<one_to_n_label> reference(timetable const& tt,
                                      one_to_n_targets const& targets,
                                      one_to_n_query const& q,
                                      cell_idx_t const o,
                                      cell_idx_t const d,
                                      unixtime_t const dep) {
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

  auto rq = query{};
  rq.start_time_ = dep;
  rq.start_match_mode_ = location_match_mode::kIntermodal;
  rq.dest_match_mode_ = location_match_mode::kIntermodal;
  rq.start_ = to_offsets(targets.access_[o]);
  rq.destination_ = to_offsets(targets.egress_by_cell_[d]);
  rq.max_travel_time_ = q.max_travel_time_;
  // one_to_n has walk-only labels only from the direct cell walks: don't
  // prune with walks through a stop (start -> stop -> destination)
  rq.fastest_direct_ = kMaxTravelTime;
  rq.max_transfers_ = q.max_transfers_;
  rq.prf_idx_ = q.prf_idx_;
  rq.allowed_claszes_ = q.allowed_claszes_;
  rq.transfer_time_settings_ = q.transfer_time_settings_;

  auto search_state = routing::search_state{};
  auto algo_state = raptor_state{};
  auto const result = raptor_search(tt, nullptr, search_state, algo_state, rq,
                                    direction::kForward);

  auto best = std::map<std::uint8_t, duration_t>{};
  for (auto const& j : *result.journeys_) {
    auto const travel_time =
        std::chrono::duration_cast<duration_t>(j.arrival_time() - dep);
    if (travel_time > q.max_travel_time_) {
      continue;
    }
    auto const k = static_cast<std::uint8_t>(j.transfers_ + 1U);
    if (auto const it = best.find(k);
        it == end(best) || travel_time < it->second) {
      best[k] = travel_time;
    }
  }

  auto front = std::vector<one_to_n_label>{};
  for (auto const& [k, travel_time] : best) {
    if (front.empty() || travel_time < front.back().travel_time_) {
      front.push_back({.k_ = k, .travel_time_ = travel_time});
    }
  }
  return front;
}

std::string fmt_labels(std::vector<one_to_n_label> const& labels) {
  auto s = std::string{};
  for (auto const& l : labels) {
    s += fmt::format("({}, {}) ", l.k_, l.travel_time_.count());
  }
  return s.empty() ? "-" : s;
}

double ms(std::chrono::nanoseconds const d) {
  return std::chrono::duration<double, std::milli>{d}.count();
}

}  // namespace

int main(int argc, char** argv) {
  auto tt_path = std::filesystem::path{};
  auto bbox_str = std::string{};
  auto spacing = 1000.0;
  auto walks = beeline_walks{};
  auto max_walk = duration_t::rep{10};
  auto max_direct = duration_t::rep{-1};
  auto start_str = std::string{};
  auto window = duration_t::rep{60};
  auto n_samples = 6U;
  auto sampling = std::string{"systematic"};
  auto max_travel_time = duration_t::rep{90};
  auto max_transfers = std::uint32_t{kMaxTransfers};
  auto acc_str = std::string{"histogram"};
  auto bin_width = duration_t::rep{5};
  auto n_bins = 0U;
  auto max_k = std::uint32_t{kMaxTransfers + 1U};
  auto out_path = std::string{};
  auto n_threads = 1U;
  auto n_origins = 0U;
  auto seed = std::uint64_t{0U};
  auto no_prune = false;
  auto n_validate = 0U;

  auto desc = bpo::options_description{"Options"};
  desc.add_options()("help,h", "produce this help message")  //
      ("tt_path,p", bpo::value(&tt_path)->required(), "timetable path")  //
      ("bbox,b", bpo::value(&bbox_str)->required(),
       "grid area: lat_min,lon_min,lat_max,lon_max | stops (all served "
       "stops)")  //
      ("spacing", bpo::value(&spacing)->default_value(spacing),
       "grid spacing in meters")  //
      ("walk_speed", bpo::value(&walks.speed_)->default_value(walks.speed_),
       "beeline walking speed in m/s")  //
      ("max_walk", bpo::value(&max_walk)->default_value(max_walk),
       "max walk between a cell and a stop in minutes")  //
      ("max_direct", bpo::value(&max_direct)->default_value(max_direct),
       "max walk-only connection between cells in minutes (0 = none, "
       "-1 = max_walk)")  //
      ("start", bpo::value(&start_str)->required(),
       "start of the departure window (UTC), e.g. 2024-05-14T06:00")  //
      ("window", bpo::value(&window)->default_value(window),
       "length of the departure window in minutes")  //
      ("samples", bpo::value(&n_samples)->default_value(n_samples),
       "number of departure times per origin")  //
      ("sampling", bpo::value(&sampling)->default_value(sampling),
       "systematic (fixed interval) | random (uniform in the window)")  //
      ("max_travel_time,t",
       bpo::value(&max_travel_time)->default_value(max_travel_time),
       "max travel time in minutes")  //
      ("max_transfers",
       bpo::value(&max_transfers)->default_value(max_transfers),
       "max transfers")  //
      ("accumulator,a", bpo::value(&acc_str)->default_value(acc_str),
       "histogram | raw | none")  //
      ("bin_width", bpo::value(&bin_width)->default_value(bin_width),
       "histogram bin width in minutes")  //
      ("bins", bpo::value(&n_bins)->default_value(n_bins),
       "histogram bins (0 = up to the max travel time)")  //
      ("max_k", bpo::value(&max_k)->default_value(max_k),
       "histogram: rows for at most 0..max_k trips")  //
      ("out,o", bpo::value(&out_path),
       "output CSV (required unless -a none)")  //
      ("threads,j", bpo::value(&n_threads)->default_value(n_threads),
       "worker threads")  //
      ("origins", bpo::value(&n_origins)->default_value(n_origins),
       "random subset of origin cells (0 = all)")  //
      ("seed", bpo::value(&seed)->default_value(seed),
       "seed for random origins / samples / validation")  //
      ("no_prune", bpo::bool_switch(&no_prune),
       "disable lower bounds from the origins")  //
      ("validate", bpo::value(&n_validate)->default_value(n_validate),
       "compare N random (origin, destination, departure) with the "
       "intermodal search, then exit");

  auto vm = bpo::variables_map{};
  bpo::store(bpo::command_line_parser(argc, argv).options(desc).run(), vm);
  if (vm.count("help") != 0U) {
    std::cout << desc << "\n";
    return 0;
  }
  bpo::notify(vm);

  auto bbox =
      bbox_str == "stops" ? std::optional{geo::box{}} : parse_bbox(bbox_str);
  auto const start = parse_time(start_str);
  if (!bbox.has_value() || !start.has_value()) {
    std::cerr << "malformed --bbox or --start\n";
    return 1;
  }
  if (acc_str != "histogram" && acc_str != "raw" && acc_str != "none") {
    std::cerr << "unknown accumulator " << acc_str << "\n";
    return 1;
  }
  if (n_validate == 0U && acc_str != "none" && out_path.empty()) {
    std::cerr << "--out required for accumulator " << acc_str << "\n";
    return 1;
  }
  walks.max_walk_ = duration_t{max_walk};
  walks.max_direct_ = duration_t{max_direct < 0 ? max_walk : max_direct};

  std::cerr << "loading timetable...\n";
  auto tt = *timetable::read(tt_path);
  tt.resolve();

  if (bbox_str == "stops") {
    for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
      if (!tt.location_routes_[l].empty()) {
        bbox->extend(tt.locations_.coordinates_[l]);
      }
    }
  }

  auto const t0 = std::chrono::steady_clock::now();
  auto const cells = make_grid(*bbox, spacing);
  auto const targets = make_beeline_targets(tt, cells, walks);
  auto n_with_stops = 0U;
  auto n_egress = std::size_t{0U};
  for (auto c = 0U; c != targets.n_cells(); ++c) {
    n_with_stops += targets.access_[cell_idx_t{c}].empty() ? 0U : 1U;
    n_egress += targets.egress_by_cell_[cell_idx_t{c}].size();
  }
  std::cerr << fmt::format(
      "grid: {} cells, {} with stops, {:.1f} egress walks per cell with "
      "stops, built in {:.0f} ms\n",
      targets.n_cells(), n_with_stops,
      n_with_stops == 0U ? 0.0 : static_cast<double>(n_egress) / n_with_stops,
      ms(std::chrono::steady_clock::now() - t0));

  auto rng = std::mt19937_64{seed};
  auto q = one_to_n_query{};
  q.max_travel_time_ = duration_t{max_travel_time};
  q.max_transfers_ = static_cast<std::uint8_t>(max_transfers);
  q.prune_unreachable_cells_ = !no_prune;
  q.n_threads_ = n_threads;
  for (auto i = 0U; i != n_samples; ++i) {
    if (sampling == "random") {
      auto dist = std::uniform_int_distribution<duration_t::rep>{
          0, static_cast<duration_t::rep>(window - 1)};
      q.departures_.push_back(*start + duration_t{dist(rng)});
    } else {
      q.departures_.push_back(*start + duration_t{static_cast<duration_t::rep>(
                                           i * window / n_samples)});
    }
  }

  auto all_origins = std::vector<cell_idx_t>{};
  for (auto c = 0U; c != targets.n_cells(); ++c) {
    all_origins.emplace_back(c);
  }
  if (n_origins != 0U && n_origins < all_origins.size()) {
    std::shuffle(begin(all_origins), end(all_origins), rng);
    all_origins.resize(n_origins);
    std::sort(begin(all_origins), end(all_origins));
  }
  q.origins_ = all_origins;

  if (n_validate != 0U) {
    auto pick_cell =
        std::uniform_int_distribution<std::size_t>{0U, q.origins_.size() - 1U};
    auto pick_dest =
        std::uniform_int_distribution<std::uint32_t>{0U, targets.n_cells() - 1};
    auto pick_dep = std::uniform_int_distribution<std::size_t>{
        0U, q.departures_.size() - 1U};
    auto n_ok = 0U;
    auto n_nonempty = 0U;
    for (auto i = 0U; i != n_validate; ++i) {
      auto const o = q.origins_[pick_cell(rng)];
      auto d = cell_idx_t{pick_dest(rng)};
      if (d == o) {
        d = cell_idx_t{(to_idx(d) + 1U) % targets.n_cells()};
      }
      auto const dep = q.departures_[pick_dep(rng)];

      auto single = q;
      single.origins_ = {o};
      single.departures_ = {dep};
      single.n_threads_ = 1U;
      auto m = std::map<cell_idx_t, std::vector<one_to_n_label>>{};
      one_to_n(tt, nullptr, targets, single, collector{m});

      // walk-only labels are not produced by the intermodal search
      auto actual = m[d];
      std::erase_if(actual, [](one_to_n_label const& l) { return l.k_ == 0U; });
      auto const expected = reference(tt, targets, q, o, d, dep);
      auto const has_walk = !m[d].empty() && m[d].front().k_ == 0U;
      auto ok = actual == expected;
      if (!ok && has_walk) {
        // transit labels dominated by walking are suppressed
        auto filtered = expected;
        std::erase_if(filtered, [&](one_to_n_label const& l) {
          return l.travel_time_ >= m[d].front().travel_time_;
        });
        ok = actual == filtered;
      }
      n_ok += ok ? 1U : 0U;
      n_nonempty += expected.empty() ? 0U : 1U;
      if (!ok) {
        std::cout << fmt::format(
            "MISMATCH o={} d={} dep={}: one_to_n={} intermodal={}\n", to_idx(o),
            to_idx(d), date::format("%FT%R", dep), fmt_labels(m[d]),
            fmt_labels(expected));
      }
    }
    std::cout << fmt::format("validated {} / {} ({} with transit labels)\n",
                             n_ok, n_validate, n_nonempty);
    return n_ok == n_validate ? 0 : 1;
  }

  auto out = std::ofstream{};
  if (!out_path.empty()) {
    out.open(out_path);
    utl::verify(out.is_open(), "cannot open {}", out_path);
  }
  auto mutex = std::mutex{};
  auto acc = std::unique_ptr<one_to_n_accumulator>{};
  if (acc_str == "histogram") {
    auto const config = one_to_n_histogram::config{
        .n_cells_ = targets.n_cells(),
        .bin_width_ = duration_t{bin_width},
        .n_bins_ = n_bins != 0U ? n_bins
                                : static_cast<std::uint32_t>(
                                      max_travel_time / bin_width + 1),
        .max_k_ = static_cast<std::uint8_t>(max_k)};
    out << one_to_n_histogram::header(config);
    acc = std::make_unique<one_to_n_histogram>(config, out, mutex);
  } else if (acc_str == "raw") {
    out << one_to_n_raw_writer::kHeader;
    acc = std::make_unique<one_to_n_raw_writer>(out, mutex);
  } else {
    acc = std::make_unique<null_accumulator>();
  }

  auto const stats = one_to_n(tt, nullptr, targets, q, *acc);

  std::cout << fmt::format(
      "origins: {}\n"
      "searches: {} ({} skipped: no cell reachable)\n"
      "labels: {} ({:.1f} per search)\n"
      "rounds with target pruning bound: {} ({:.2f} per search)\n"
      "total: {:.0f} ms wall\n"
      "search: {:.0f} ms ({:.2f} ms per search, summed over threads)\n"
      "origin lower bounds: {:.0f} ms ({:.2f} ms per origin)\n"
      "accumulator: {:.0f} ms\n",
      stats.n_origins_, stats.n_searches_, stats.n_searches_skipped_,
      stats.n_labels_,
      static_cast<double>(stats.n_labels_) /
          static_cast<double>(std::max(stats.n_searches_, std::uint64_t{1U})),
      stats.n_bound_rounds_,
      static_cast<double>(stats.n_bound_rounds_) /
          static_cast<double>(std::max(stats.n_searches_, std::uint64_t{1U})),
      ms(stats.total_time_), ms(stats.search_time_),
      ms(stats.search_time_) /
          static_cast<double>(std::max(stats.n_searches_, std::uint64_t{1U})),
      ms(stats.origin_lb_time_),
      ms(stats.origin_lb_time_) /
          static_cast<double>(std::max(stats.n_origins_, std::uint64_t{1U})),
      ms(stats.accumulator_time_));
}
