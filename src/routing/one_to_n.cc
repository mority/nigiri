#include "nigiri/routing/one_to_n.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <numbers>
#include <thread>

#include "geo/point_rtree.h"

#include "utl/helpers/algorithm.h"
#include "utl/verify.h"

#include "nigiri/common/delta_t.h"
#include "nigiri/for_each_meta.h"
#include "nigiri/routing/dijkstra.h"
#include "nigiri/routing/one_to_all.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

namespace {

constexpr auto const kVias = via_offset_t{0U};
constexpr auto const kInvalid = kInvalidDelta<direction::kForward>;
constexpr auto const kUnreachable = std::numeric_limits<label::dist_t>::max();

using clock = std::chrono::steady_clock;

// Expands like location_match_mode::kIntermodal, keeps the shortest walk per
// location, sorted by location.
std::vector<stop_walk> expand(timetable const& tt,
                              std::vector<stop_walk> const& walks) {
  auto expanded = std::vector<stop_walk>{};
  for (auto const& w : walks) {
    for_each_meta(
        tt, location_match_mode::kIntermodal, w.location_,
        [&](location_idx_t const l) {
          expanded.push_back({.location_ = l, .duration_ = w.duration_});
        });
  }
  utl::sort(expanded, [](stop_walk const& a, stop_walk const& b) {
    return std::tie(a.location_, a.duration_) <
           std::tie(b.location_, b.duration_);
  });
  expanded.erase(std::unique(begin(expanded), end(expanded),
                             [](stop_walk const& a, stop_walk const& b) {
                               return a.location_ == b.location_;
                             }),
                 end(expanded));
  return expanded;
}

// Shortest walk per cell, without walks to `self`, sorted by cell.
std::vector<cell_walk> dedupe(std::vector<cell_walk> walks,
                              cell_idx_t const self) {
  std::erase_if(walks, [&](cell_walk const& w) { return w.cell_ == self; });
  utl::sort(walks, [](cell_walk const& a, cell_walk const& b) {
    return std::tie(a.cell_, a.duration_) < std::tie(b.cell_, b.duration_);
  });
  walks.erase(std::unique(begin(walks), end(walks),
                          [](cell_walk const& a, cell_walk const& b) {
                            return a.cell_ == b.cell_;
                          }),
              end(walks));
  return walks;
}

duration_t walk_duration(double const distance, double const speed) {
  return duration_t{
      static_cast<duration_t::rep>(std::ceil(distance / speed / 60.0))};
}

date::sys_days to_sys_days(timetable const& tt, day_idx_t const d) {
  return tt.internal_interval_days().from_ +
         static_cast<int>(to_idx(d)) * date::days{1};
}

std::vector<offset> to_offsets(auto const& walks) {
  auto offsets = std::vector<offset>{};
  offsets.reserve(walks.size());
  for (auto const& w : walks) {
    offsets.emplace_back(w.location_, w.duration_,
                         transport_mode_t::payload_t{0U});
  }
  return offsets;
}

template <bool Rt>
struct worker {
  using algo_t = raptor<direction::kForward, Rt, kVias, search_mode::kOneToN>;

  worker(timetable const& tt,
         rt_timetable const* rtt,
         one_to_n_targets const& targets,
         one_to_n_query const& q,
         std::vector<unixtime_t> const& departures,
         std::vector<std::uint16_t> const& lb_to_cells,
         one_to_n_accumulator const& acc)
      : tt_{tt},
        rtt_{rtt},
        targets_{targets},
        q_{q},
        departures_{departures},
        lb_to_cells_{lb_to_cells},
        acc_{acc.clone()},
        n_cells_{targets.n_cells()},
        is_dest_{tt.n_locations()} {
    otn_.egress_ = &targets_.egress_;
    otn_.best_.resize(n_cells_, kInvalid);
    otn_.improved_.resize(n_cells_);
    otn_.relevant_.resize(n_cells_);
    sample_.offsets_.resize(n_cells_ + 1U);
    cursor_.resize(n_cells_);
    lb_q_.dest_match_mode_ = location_match_mode::kExact;
    lb_q_.prf_idx_ = q_.prf_idx_;
    lb_q_.max_travel_time_ = q_.max_travel_time_;
  }

  void process(cell_idx_t const o) {
    ++stats_.n_origins_;
    acc_->begin_origin(o);
    compute_relevant(o);
    for (auto const dep : departures_) {
      search(o, dep);
      auto const acc_start = clock::now();
      acc_->add_sample(dep, sample_);
      stats_.accumulator_time_ += clock::now() - acc_start;
    }
    auto const acc_start = clock::now();
    acc_->end_origin(o);
    stats_.accumulator_time_ += clock::now() - acc_start;
  }

  // Relevant: every cell except the origin that is not ruled out by a lower
  // bound on the travel time from the origin.
  void compute_relevant(cell_idx_t const o) {
    auto& relevant = otn_.relevant_;
    if (!q_.prune_unreachable_cells_) {
      // Still trivially unreachable by a ride: cells without stops.
      otn_.n_relevant_ = 0U;
      for (auto c = 0U; c != n_cells_; ++c) {
        auto const r =
            c != to_idx(o) && !targets_.egress_by_cell_[cell_idx_t{c}].empty();
        relevant.set(c, r);
        otn_.n_relevant_ += r ? 1U : 0U;
      }
      return;
    }

    auto const start = clock::now();
    lb_q_.destination_ = to_offsets(targets_.access_[o]);
    dijkstra(tt_, lb_q_, tt_.bwd_search_lb_graph_[q_.prf_idx_],
             rtt_ == nullptr ? nullptr : &rtt_->bwd_search_lb_graph_has_edges_,
             rtt_ == nullptr ? nullptr : &rtt_->bwd_search_lb_graph_,
             lb_from_origin_,
             static_cast<label::dist_t>(q_.max_travel_time_.count() + 1));

    utl::fill(relevant.blocks_, 0U);
    otn_.n_relevant_ = 0U;
    for (auto c = 0U; c != n_cells_; ++c) {
      if (c == to_idx(o)) {
        continue;
      }
      for (auto const& w : targets_.egress_by_cell_[cell_idx_t{c}]) {
        auto const lb = lb_from_origin_[to_idx(w.location_)];
        if (lb != kUnreachable &&
            lb + w.duration_.count() <= q_.max_travel_time_.count()) {
          relevant.set(c, true);
          ++otn_.n_relevant_;
          break;
        }
      }
    }
    stats_.origin_lb_time_ += clock::now() - start;
  }

  void search(cell_idx_t const o, unixtime_t const dep) {
    auto const start = clock::now();

    auto const base = make_base(tt_, dep);
    auto const dep_delta = unix_to_delta(to_sys_days(tt_, base), dep);
    auto const max_travel_time = q_.max_travel_time_.count();

    utl::fill(otn_.best_, kInvalid);
    otn_.emitted_.clear();
    otn_.n_reached_ = 0U;
    otn_.n_bound_rounds_ = 0U;

    otn_.best_[to_idx(o)] = dep_delta;

    // Walk only: labels with k = 0.
    for (auto const& w : targets_.direct_[o]) {
      if (w.duration_.count() > max_travel_time) {
        continue;
      }
      auto const c = to_idx(w.cell_);
      if (otn_.relevant_.test(c)) {
        ++otn_.n_reached_;
      }
      otn_.best_[c] = clamp(dep_delta + w.duration_.count());
      otn_.emitted_.push_back(
          {.cell_ = w.cell_, .k_ = 0U, .time_ = otn_.best_[c]});
    }

    if (otn_.n_relevant_ == 0U) {
      ++stats_.n_searches_skipped_;
    } else {
      auto algo = algo_t{tt_,
                         rtt_,
                         state_,
                         is_dest_,
                         is_via_,
                         dist_to_dest_,
                         td_dist_to_dest_,
                         lb_to_cells_,
                         via_stops_,
                         base,
                         q_.allowed_claszes_,
                         q_.require_bike_transport_,
                         q_.require_car_transport_,
                         q_.prf_idx_ == kWheelchairProfile,
                         q_.no_compulsory_reservation_,
                         q_.transfer_time_settings_,
                         q_.prf_idx_};
      algo.set_one_to_n(otn_);
      algo.next_start_time();
      for (auto const& w : targets_.access_[o]) {
        if (w.duration_.count() <= max_travel_time) {
          algo.add_start(w.location_, dep + w.duration_);
        }
      }
      algo.execute(dep, q_.max_transfers_, dep + q_.max_travel_time_, results_);
    }

    ++stats_.n_searches_;
    stats_.n_labels_ += otn_.emitted_.size();
    stats_.n_bound_rounds_ += otn_.n_bound_rounds_;

    make_sample(dep_delta);
    stats_.search_time_ += clock::now() - start;
  }

  // Groups the emitted labels by cell (counting sort, keeps the k order).
  void make_sample(delta_t const dep_delta) {
    auto& offsets = sample_.offsets_;
    utl::fill(offsets, 0U);
    for (auto const& e : otn_.emitted_) {
      ++offsets[to_idx(e.cell_) + 1U];
    }

    sample_.reached_.clear();
    for (auto c = 0U; c != n_cells_; ++c) {
      if (offsets[c + 1U] != 0U) {
        sample_.reached_.emplace_back(c);
      }
      offsets[c + 1U] += offsets[c];
    }

    std::copy(begin(offsets), std::prev(end(offsets)), begin(cursor_));
    sample_.labels_.resize(otn_.emitted_.size());
    for (auto const& e : otn_.emitted_) {
      sample_.labels_[cursor_[to_idx(e.cell_)]++] = {
          .k_ = e.k_,
          .travel_time_ =
              duration_t{static_cast<duration_t::rep>(e.time_ - dep_delta)}};
    }
  }

  timetable const& tt_;
  rt_timetable const* rtt_;
  one_to_n_targets const& targets_;
  one_to_n_query const& q_;
  std::vector<unixtime_t> const& departures_;
  std::vector<std::uint16_t> lb_to_cells_;  // copy: raptor takes non-const&
  std::unique_ptr<one_to_n_accumulator> acc_;
  std::uint32_t n_cells_;

  raptor_state state_;
  bitvec is_dest_;
  std::array<bitvec, kMaxVias> is_via_;
  std::vector<std::uint16_t> dist_to_dest_;
  hash_map<location_idx_t, std::vector<td_offset>> td_dist_to_dest_;
  std::vector<via_stop> via_stops_;
  pareto_set<journey> results_;

  query lb_q_;
  std::vector<std::uint16_t> lb_from_origin_;

  one_to_n_state otn_;
  one_to_n_sample sample_;
  std::vector<std::uint32_t> cursor_;

  one_to_n_stats stats_;
};

void add(one_to_n_stats& a, one_to_n_stats const& b) {
  a.n_origins_ += b.n_origins_;
  a.n_searches_ += b.n_searches_;
  a.n_searches_skipped_ += b.n_searches_skipped_;
  a.n_labels_ += b.n_labels_;
  a.n_bound_rounds_ += b.n_bound_rounds_;
  a.origin_lb_time_ += b.origin_lb_time_;
  a.search_time_ += b.search_time_;
  a.accumulator_time_ += b.accumulator_time_;
}

// Lower bound from each location to the nearest cell (including the egress
// walk), independent of the origin.
std::vector<std::uint16_t> lb_to_cells(timetable const& tt,
                                       rt_timetable const* rtt,
                                       one_to_n_targets const& targets,
                                       one_to_n_query const& q) {
  auto lb_q = query{};
  lb_q.dest_match_mode_ = location_match_mode::kExact;
  lb_q.prf_idx_ = q.prf_idx_;
  lb_q.max_travel_time_ = q.max_travel_time_;
  for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
    auto const egress = targets.egress_[l];
    if (egress.empty()) {
      continue;
    }
    auto const shortest = std::min_element(
        begin(egress), end(egress), [](cell_walk const& a, cell_walk const& b) {
          return a.duration_ < b.duration_;
        });
    lb_q.destination_.emplace_back(l, shortest->duration_,
                                   transport_mode_t::payload_t{0U});
  }

  auto lb = std::vector<std::uint16_t>{};
  dijkstra(tt, lb_q, tt.fwd_search_lb_graph_[q.prf_idx_],
           rtt == nullptr ? nullptr : &rtt->fwd_search_lb_graph_has_edges_,
           rtt == nullptr ? nullptr : &rtt->fwd_search_lb_graph_, lb,
           static_cast<label::dist_t>(q.max_travel_time_.count() + 1));
  return lb;
}

template <bool Rt>
one_to_n_stats run(timetable const& tt,
                   rt_timetable const* rtt,
                   one_to_n_targets const& targets,
                   one_to_n_query const& q,
                   one_to_n_accumulator const& acc) {
  auto const start = clock::now();

  auto departures = q.departures_;
  utl::sort(departures);

  auto origins = q.origins_;
  if (origins.empty()) {
    origins.resize(targets.n_cells());
    for (auto c = 0U; c != targets.n_cells(); ++c) {
      origins[c] = cell_idx_t{c};
    }
  }

  auto const lb = lb_to_cells(tt, rtt, targets, q);

  auto stats = one_to_n_stats{};
  auto const n_threads = std::max(1U, q.n_threads_);
  if (n_threads == 1U) {
    auto w = worker<Rt>{tt, rtt, targets, q, departures, lb, acc};
    for (auto const o : origins) {
      w.process(o);
    }
    stats = w.stats_;
  } else {
    auto next = std::atomic_size_t{0U};
    auto mutex = std::mutex{};
    auto error = std::exception_ptr{};
    auto threads = std::vector<std::thread>{};
    for (auto i = 0U; i != n_threads; ++i) {
      threads.emplace_back([&]() {
        try {
          auto w = worker<Rt>{tt, rtt, targets, q, departures, lb, acc};
          for (auto j = next++; j < origins.size(); j = next++) {
            w.process(origins[j]);
          }
          auto const lock = std::lock_guard{mutex};
          add(stats, w.stats_);
        } catch (...) {
          next = origins.size();
          auto const lock = std::lock_guard{mutex};
          error = std::current_exception();
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
    if (error) {
      std::rethrow_exception(error);
    }
  }

  stats.total_time_ = clock::now() - start;
  return stats;
}

}  // namespace

one_to_n_targets make_one_to_n_targets(
    timetable const& tt,
    std::vector<std::vector<stop_walk>> const& access,
    std::vector<std::vector<stop_walk>> const& egress,
    std::vector<std::vector<cell_walk>> const& direct) {
  auto const n_cells = access.size();
  utl::verify(egress.empty() || egress.size() == n_cells,
              "one_to_n: {} egress lists for {} cells", egress.size(), n_cells);
  utl::verify(direct.empty() || direct.size() == n_cells,
              "one_to_n: {} direct walk lists for {} cells", direct.size(),
              n_cells);

  auto t = one_to_n_targets{};
  auto egress_by_location =
      std::vector<std::vector<cell_walk>>(tt.n_locations());
  for (auto i = 0U; i != n_cells; ++i) {
    auto const c = cell_idx_t{i};

    auto const a = expand(tt, access[i]);
    t.access_.emplace_back(a);

    auto const e = egress.empty() ? a : expand(tt, egress[i]);
    t.egress_by_cell_.emplace_back(e);
    for (auto const& w : e) {
      egress_by_location[to_idx(w.location_)].push_back(
          {.cell_ = c, .duration_ = w.duration_});
    }

    t.direct_.emplace_back(direct.empty() ? std::vector<cell_walk>{}
                                          : dedupe(direct[i], c));
  }
  for (auto const& e : egress_by_location) {
    t.egress_.emplace_back(e);
  }
  return t;
}

std::vector<geo::latlng> make_grid(geo::box const& b, double const spacing) {
  constexpr auto const kMetersPerDegree = 111'319.49;
  auto const center_lat = (b.min_.lat_ + b.max_.lat_) / 2.0;
  auto const d_lat = spacing / kMetersPerDegree;
  auto const d_lng =
      spacing /
      (kMetersPerDegree * std::cos(center_lat * std::numbers::pi / 180.0));

  auto grid = std::vector<geo::latlng>{};
  for (auto lat = b.min_.lat_; lat <= b.max_.lat_; lat += d_lat) {
    for (auto lng = b.min_.lng_; lng <= b.max_.lng_; lng += d_lng) {
      grid.emplace_back(lat, lng);
    }
  }
  return grid;
}

one_to_n_targets make_beeline_targets(timetable const& tt,
                                      std::span<geo::latlng const> cells,
                                      beeline_walks const& w) {
  auto stops = std::vector<location_idx_t>{};
  for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
    if (!tt.location_routes_[l].empty()) {
      stops.push_back(l);
    }
  }
  auto const stop_rtree = geo::make_point_rtree(
      stops,
      [&](location_idx_t const l) { return tt.locations_.coordinates_[l]; });

  auto const max_walk = w.speed_ * 60.0 * w.max_walk_.count();
  auto access = std::vector<std::vector<stop_walk>>(cells.size());
  for (auto i = 0U; i != cells.size(); ++i) {
    for (auto const& [dist, s] :
         stop_rtree.in_radius_with_distance(cells[i], max_walk)) {
      access[i].push_back(
          {.location_ = stops[s], .duration_ = walk_duration(dist, w.speed_)});
    }
  }

  auto direct = std::vector<std::vector<cell_walk>>{};
  if (w.max_direct_.count() > 0) {
    auto const cell_rtree = geo::make_point_rtree(cells);
    auto const max_direct = w.speed_ * 60.0 * w.max_direct_.count();
    direct.resize(cells.size());
    for (auto i = 0U; i != cells.size(); ++i) {
      for (auto const& [dist, c] :
           cell_rtree.in_radius_with_distance(cells[i], max_direct)) {
        if (c != i) {
          direct[i].push_back(
              {.cell_ = cell_idx_t{static_cast<cell_idx_t::value_t>(c)},
               .duration_ = walk_duration(dist, w.speed_)});
        }
      }
    }
  }

  return make_one_to_n_targets(tt, access, {}, direct);
}

one_to_n_stats one_to_n(timetable const& tt,
                        rt_timetable const* rtt,
                        one_to_n_targets const& targets,
                        one_to_n_query const& q,
                        one_to_n_accumulator const& acc) {
  utl::verify(targets.egress_.size() == tt.n_locations(),
              "one_to_n: targets built for another timetable");
  for (auto const o : q.origins_) {
    utl::verify(to_idx(o) < targets.n_cells(), "one_to_n: invalid origin {}",
                to_idx(o));
  }
  utl::verify(q.max_travel_time_ > duration_t{0} &&
                  q.max_travel_time_ <= kMaxTravelTime,
              "one_to_n: max travel time {} min out of range",
              q.max_travel_time_.count());
  return rtt == nullptr ? run<false>(tt, rtt, targets, q, acc)
                        : run<true>(tt, rtt, targets, q, acc);
}

}  // namespace nigiri::routing
