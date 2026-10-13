#include "nigiri/routing/raptor/reversed.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <string>

#include "utl/erase_if.h"
#include "utl/helpers/algorithm.h"
#include "utl/nwise.h"
#include "utl/verify.h"

#include "nigiri/logging.h"
#include "nigiri/routing/dijkstra.h"
#include "nigiri/routing/direct.h"
#include "nigiri/routing/direct_filter.h"
#include "nigiri/routing/get_fastest_direct.h"
#include "nigiri/routing/interval_estimate.h"
#include "nigiri/routing/start_times.h"
#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/rt/frun.h"
#include "nigiri/special_stations.h"

namespace nigiri::routing {

namespace {

// Journeys found by a search against the query direction carry the time at
// the query's destination as start_time_ and the special stations swapped.
inline void to_query_orientation(journey& j) {
  auto const swap_special = [](location_idx_t const l) -> location_idx_t {
    switch (to_idx(l)) {
      case to_idx(get_special_station(special_station::kStart)):
        return get_special_station(special_station::kEnd);
      case to_idx(get_special_station(special_station::kEnd)):
        return get_special_station(special_station::kStart);
      default: return l;
    }
  };
  std::swap(j.start_time_, j.dest_time_);
  if (!j.legs_.empty()) {
    j.legs_.front().from_ = swap_special(j.legs_.front().from_);
    j.legs_.back().to_ = swap_special(j.legs_.back().to_);
  }
}

// Slow direct journeys, sort by start time and, forward only, the
// earliest-alternative pass of pong_search: backward reconstruction yields
// the latest possible intermediate legs.
template <direction SearchDir, via_offset_t Vias>
void finalize_results(timetable const& tt,
                      rt_timetable const* rtt,
                      query const& q,
                      interval<unixtime_t> const& result_itv,
                      pareto_set<journey>& results) {
  enrich_with_slow_direct<SearchDir>(tt, rtt, q, result_itv, results);

  utl::sort(results, [](journey const& a, journey const& b) {
    return std::tuple{a.start_time_, a.transfers_} <
           std::tuple{b.start_time_, b.transfers_};
  });

  if constexpr (SearchDir == direction::kForward && Vias == 0U) {
    for (auto& j : results) {
      for (auto const [transit_1, transfer_1, transit_2, transfer_2,
                       transit_3] : utl::nwise<5>(j.legs_)) {
        if (!std::holds_alternative<journey::run_enter_exit>(
                transit_1.uses_) ||
            !std::holds_alternative<journey::run_enter_exit>(
                transit_2.uses_) ||
            !std::holds_alternative<journey::run_enter_exit>(
                transit_3.uses_)) {
          continue;
        }

        auto const& front =
            std::get<journey::run_enter_exit>(transit_1.uses_);
        auto const& back = std::get<journey::run_enter_exit>(transit_3.uses_);

        auto const front_r = rt::frun{tt, rtt, front.r_};
        auto const from = front_r[front.stop_range_.to_ - 1U];
        auto const arr_time = from.time(event_type::kArr);

        auto const back_r = rt::frun{tt, rtt, back.r_};
        auto const to = back_r[back.stop_range_.from_];
        auto const dep_time = to.time(event_type::kDep);

        auto const earlier =
            get_earliest_alternative(tt, rtt, q, from.get_location_idx(),
                                     to.get_location_idx(), arr_time, dep_time);
        if (earlier.has_value()) {
          transfer_1 = earlier->at(0);
          transit_2 = earlier->at(1);
          transfer_2 = earlier->at(2);
        }
      }
    }
  }
}

constexpr auto const kLookAheadFactor = 1.5;  // Halm 2019: rrdep 1.5

template <direction SearchDir, bool Rt, via_offset_t Vias>
routing_result reversed(timetable const& tt,
                        rt_timetable const* rtt,
                        search_state& s_state,
                        raptor_state& r_state,
                        query q,
                        std::optional<std::chrono::seconds> const timeout) {
  constexpr auto const kFwd = (SearchDir == direction::kForward);
  constexpr auto const kScanDir = flip(SearchDir);
  using ea_algo_t = raptor<SearchDir, Rt, Vias, search_mode::kOneToOne>;
  using scan_algo_t = raptor<kScanDir, Rt, Vias, search_mode::kOneToOne>;

  utl::verify(holds_alternative<interval<unixtime_t>>(q.start_time_),
              "reversed search: start time interval required");
  utl::verify(q.min_connection_count_ > 0U,
              "reversed search: min_connection_count > 0 required");
  utl::verify(kFwd ? (q.extend_interval_later_ && !q.extend_interval_earlier_)
                   : (q.extend_interval_earlier_ && !q.extend_interval_later_),
              "reversed search: open end in search direction required");

  auto const processing_start_time = std::chrono::steady_clock::now();
  auto const itv = std::get<interval<unixtime_t>>(q.start_time_);

  // Forward: the earliest departure; backward: the latest arrival.
  auto const bound = kFwd ? itv.from_
                     : itv.size() == 0_minutes ? itv.to_
                                               : itv.to_ - duration_t{1};

  auto stats = search_stats{};
  auto algo_stats = std::map<std::string, std::uint64_t>{};
  auto const add_stats = [&](routing_result const& r) {
    stats.lb_time_ += r.search_stats_.lb_time_;
    stats.fastest_direct_ = r.search_stats_.fastest_direct_;
    stats.interval_extensions_ += r.search_stats_.interval_extensions_;
    stats.n_events_skipped_by_early_termination_ +=
        r.search_stats_.n_events_skipped_by_early_termination_;
    stats.search_interval_reduction_by_early_termination_ +=
        r.search_stats_.search_interval_reduction_by_early_termination_;
    stats.n_execute_fwd_ += r.search_stats_.n_execute_fwd_;
    stats.n_execute_bwd_ += r.search_stats_.n_execute_bwd_;
    for (auto const& [k, v] : r.algo_stats_) {
      algo_stats[k] += v;
    }
  };
  auto const finish = [&](interval<unixtime_t> const result_itv) {
    stats.execute_time_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - processing_start_time);
    return routing_result{.journeys_ = &s_state.results_,
                          .interval_ = result_itv,
                          .search_stats_ = stats,
                          .algo_stats_ = algo_stats};
  };
  auto const remaining_timeout =
      [&]() -> std::optional<std::chrono::seconds> {
    if (!timeout.has_value()) {
      return std::nullopt;
    }
    auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - processing_start_time);
    return std::max(std::chrono::seconds{0}, *timeout - elapsed);
  };

  // ====
  // 1. One search in query direction from the bound: forward the earliest
  //    arrival, backward the latest departure. This is where the scan starts.
  // ----
  auto ea_q = q;
  ea_q.start_time_ = bound;
  ea_q.min_connection_count_ = 0U;
  ea_q.extend_interval_earlier_ = false;
  ea_q.extend_interval_later_ = false;
  auto const ea =
      search<SearchDir, ea_algo_t>{tt, rtt, s_state, r_state, ea_q, timeout}
          .execute();
  add_stats(ea);

  auto first = std::optional<unixtime_t>{};
  for (auto const& j : *ea.journeys_) {
    first = first.has_value()
                ? (kFwd ? std::min(*first, j.dest_time_)
                        : std::max(*first, j.dest_time_))
                : std::optional{j.dest_time_};
  }
  if (!first.has_value()) {
    s_state.results_.clear();
    return finish(itv);
  }

  // ====
  // 2. Range scan against the query direction, starting at the first
  //    arrival (forward) / latest departure (backward), open towards the
  //    query's open end. The search driver's early termination stops it at
  //    min_connection_count_ journeys; the interval estimator only sizes the
  //    batches of start labels and the extensions if a batch runs out.
  // ----
  auto scan_q = q;
  scan_q.flip_dir();
  scan_q.use_start_footpaths_ =
      scan_q.start_match_mode_ != location_match_mode::kIntermodal;
  scan_q.start_time_ = interval<unixtime_t>{*first, *first + duration_t{1}};
  scan_q.max_interval_ = std::nullopt;
  scan_q.dest_time_bound_ = bound;
  auto const scan = search<kScanDir, scan_algo_t>{
      tt, rtt, s_state, r_state, scan_q, remaining_timeout()}
                        .execute();
  add_stats(scan);

  // ====
  // 3. Report in the orientation of the query, as pong_search does.
  // ----
  for (auto& j : s_state.results_) {
    to_query_orientation(j);
  }

  auto result_itv = itv;
  if (!s_state.results_.empty()) {
    auto const [min_it, max_it] =
        std::minmax_element(begin(s_state.results_), end(s_state.results_),
                            [](journey const& a, journey const& b) {
                              return a.start_time_ < b.start_time_;
                            });
    if constexpr (kFwd) {
      result_itv.to_ = std::max(itv.to_, max_it->start_time_ + duration_t{1});
    } else {
      result_itv.from_ = std::min(itv.from_, min_it->start_time_);
    }
  }

  finalize_results<SearchDir, Vias>(tt, rtt, q, result_itv, s_state.results_);

  return finish(result_itv);
}


template <typename Fwd, typename Bwd>
routing_result finish_dep(
    search_state& s_state,
    search_stats stats,
    interval<unixtime_t> const result_itv,
    std::chrono::steady_clock::time_point const processing_start_time,
    Fwd const& fwd,
    Bwd const& bwd,
    Bwd const& scan) {
  stats.execute_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - processing_start_time);
  return routing_result{
      .journeys_ = &s_state.results_,
      .interval_ = result_itv,
      .search_stats_ = stats,
      .algo_stats_ =
          (fwd.get_stats() + bwd.get_stats() + scan.get_stats()).to_map()};
}

// Departure order (Halm's rr_dep), see reversed.h. Drives the RAPTOR objects
// directly like pong_search, so that lower bounds are computed once and the
// scan keeps its round table between fills: the scan has its own
// raptor_state, the fill searches (one forward, one backward) share another.
template <direction SearchDir, bool Rt, via_offset_t Vias>
routing_result reversed_dep(timetable const& tt,
                            rt_timetable const* rtt,
                            search_state& s_state,
                            raptor_state& fill_state,
                            query q,
                            std::optional<std::chrono::seconds> const timeout) {
  constexpr auto const kFwd = (SearchDir == direction::kForward);
  constexpr auto const kScanDir = flip(SearchDir);
  using fwd_algo_t = raptor<SearchDir, Rt, Vias, search_mode::kOneToOne>;
  using bwd_algo_t = raptor<kScanDir, Rt, Vias, search_mode::kOneToOne>;

  utl::verify(holds_alternative<interval<unixtime_t>>(q.start_time_),
              "reversed search: start time interval required");
  utl::verify(q.min_connection_count_ > 0U,
              "reversed search: min_connection_count > 0 required");
  utl::verify(kFwd ? (q.extend_interval_later_ && !q.extend_interval_earlier_)
                   : (q.extend_interval_earlier_ && !q.extend_interval_later_),
              "reversed search: open end in search direction required");

  auto const processing_start_time = std::chrono::steady_clock::now();
  auto const is_timeout_reached = [&]() {
    return timeout.has_value() &&
           (std::chrono::steady_clock::now() - processing_start_time) >=
               *timeout;
  };
  auto const itv = std::get<interval<unixtime_t>>(q.start_time_);
  auto const bound = kFwd ? itv.from_
                     : itv.size() == 0_minutes ? itv.to_
                                               : itv.to_ - duration_t{1};
  auto const n = q.min_connection_count_;
  auto const m = std::max(
      n, static_cast<unsigned>(std::ceil(kLookAheadFactor * n)));

  // Query orientation: start_time_ is the time at the query's start (the
  // departure for a forward query). "Earlier" in page order is is_better.
  auto const is_better = [](unixtime_t const a, unixtime_t const b) {
    return kFwd ? a < b : a > b;
  };
  auto const step = [](int const x) { return duration_t{kFwd ? x : -x}; };
  auto const further = [&](unixtime_t const a, unixtime_t const b) {
    return is_better(a, b) ? b : a;
  };
  constexpr auto const kInfinity = kFwd ? unixtime_t::max() : unixtime_t::min();

  // ====
  // Searches: forward from a time (the fill's ping), backward from a time
  // (the fill's pong) and the backward scan, all one-to-one RAPTOR.
  // ----
  auto stats = search_stats{};
  auto const fastest_direct = get_fastest_direct(tt, q, SearchDir);
  stats.fastest_direct_ = static_cast<std::uint64_t>(fastest_direct.count());
  auto const base_day =
      day_idx_t{std::chrono::duration_cast<date::days>(
                    std::chrono::round<std::chrono::days>(bound) -
                    tt.internal_interval().from_)
                    .count()};

  auto fwd_is_dest = bitvec{};
  auto fwd_dist_to_dest = std::vector<std::uint16_t>{};
  auto fwd_is_via = std::array<bitvec, kMaxVias>{};
  collect_destinations(tt, q.destination_, q.dest_match_mode_, fwd_is_dest,
                       fwd_dist_to_dest);
  for (auto const [i, via] : utl::enumerate(q.via_stops_)) {
    collect_via_destinations(tt, via.location_, fwd_is_via[i]);
  }
  auto const lb_start = std::chrono::steady_clock::now();
  auto fwd_lb = std::vector<std::uint16_t>{};
  dijkstra(tt, q,
           (kFwd ? tt.fwd_search_lb_graph_[q.prf_idx_]
                 : tt.bwd_search_lb_graph_[q.prf_idx_]),
           (rtt == nullptr ? nullptr
                           : &(kFwd ? rtt->fwd_search_lb_graph_has_edges_
                                    : rtt->bwd_search_lb_graph_has_edges_)),
           (rtt == nullptr ? nullptr
                           : &(kFwd ? rtt->fwd_search_lb_graph_
                                    : rtt->bwd_search_lb_graph_)),
           fwd_lb);
  auto fwd = fwd_algo_t{tt,
                        rtt,
                        fill_state,
                        fwd_is_dest,
                        fwd_is_via,
                        fwd_dist_to_dest,
                        q.td_dest_,
                        fwd_lb,
                        q.via_stops_,
                        base_day,
                        q.allowed_claszes_,
                        q.require_bike_transport_,
                        q.require_car_transport_,
                        q.prf_idx_ == 2U,
                        q.no_compulsory_reservation_,
                        q.transfer_time_settings_,
                        q.prf_idx_};

  auto bq = q;  // the query against the search direction
  bq.flip_dir();
  bq.use_start_footpaths_ =
      bq.start_match_mode_ != location_match_mode::kIntermodal;
  auto bwd_is_dest = bitvec{};
  auto bwd_dist_to_dest = std::vector<std::uint16_t>{};
  auto bwd_is_via = std::array<bitvec, kMaxVias>{};
  collect_destinations(tt, bq.destination_, bq.dest_match_mode_, bwd_is_dest,
                       bwd_dist_to_dest);
  for (auto const [i, via] : utl::enumerate(bq.via_stops_)) {
    collect_via_destinations(tt, via.location_, bwd_is_via[i]);
  }
  auto bwd_lb = std::vector<std::uint16_t>{};
  dijkstra(tt, bq,
           (kFwd ? tt.bwd_search_lb_graph_[bq.prf_idx_]
                 : tt.fwd_search_lb_graph_[bq.prf_idx_]),
           (rtt == nullptr ? nullptr
                           : &(kFwd ? rtt->bwd_search_lb_graph_has_edges_
                                    : rtt->fwd_search_lb_graph_has_edges_)),
           (rtt == nullptr ? nullptr
                           : &(kFwd ? rtt->bwd_search_lb_graph_
                                    : rtt->fwd_search_lb_graph_)),
           bwd_lb);
  stats.lb_time_ = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - lb_start)
          .count());
  auto const make_bwd = [&](raptor_state& state) {
    return bwd_algo_t{tt,
                      rtt,
                      state,
                      bwd_is_dest,
                      bwd_is_via,
                      bwd_dist_to_dest,
                      bq.td_dest_,
                      bwd_lb,
                      bq.via_stops_,
                      base_day,
                      bq.allowed_claszes_,
                      bq.require_bike_transport_,
                      bq.require_car_transport_,
                      bq.prf_idx_ == 2U,
                      bq.no_compulsory_reservation_,
                      bq.transfer_time_settings_,
                      bq.prf_idx_};
  };
  auto bwd = make_bwd(fill_state);
  auto scan_state = raptor_state{};
  auto scan = make_bwd(scan_state);

  auto const max_tt = std::min(fastest_direct, q.max_travel_time_) + duration_t{1};
  auto starts = std::vector<start>{};
  auto const reconstruct = [&](auto& algo, query const& rq, journey& j) {
    try {
      algo.reconstruct(rq, j);
    } catch (std::exception const& e) {
      j.error_ = true;
      log(log_lvl::error, "search", "reconstruct failed: {}", e.what());
    }
  };

  // ====
  // State of the merge: every profile journey found so far, in query
  // orientation; the ones reported; per transfer count the frontier.
  // ----
  auto all = pareto_set<journey>{};
  using key_t = std::tuple<unixtime_t, unixtime_t, std::uint8_t>;
  auto const key = [](journey const& j) {
    return key_t{j.start_time_, j.dest_time_, j.transfers_};
  };
  auto reported = std::vector<key_t>{};
  auto const is_reported = [&](journey const& j) {
    return std::find(begin(reported), end(reported), key(j)) != end(reported);
  };
  auto const reportable = [&](journey const& j) {
    return !j.error_ && j.is_reconstructed_ &&
           j.travel_time() < fastest_direct &&
           j.travel_time() <= q.max_travel_time_ &&
           !is_not_better_than_direct(q, j);
  };
  auto frontier = std::array<unixtime_t, kMaxTransfers + 1U>{};
  frontier.fill(bound - step(1));
  // Adds a journey in search-against orientation: validates the bound,
  // reconstructs if new, converts. Returns true if it was new.
  auto const add_found = [&](auto& algo, journey&& j) {
    auto const departs_in_time =
        kFwd ? j.dest_time_ >= bound : j.dest_time_ <= bound;
    if (!departs_in_time) {
      return false;
    }
    auto probe = j;
    std::swap(probe.start_time_, probe.dest_time_);
    if (all.is_dominated(probe)) {
      return false;
    }
    reconstruct(algo, bq, j);
    to_query_orientation(j);
    return std::get<0>(all.add(std::move(j)));
  };

  // ====
  // Fill from t (one Pong iteration): the earliest arrivals per transfer
  // count for departures from t, each resolved to its latest departure.
  // Advances the frontier of every transfer count whose frontier reaches t:
  // all journeys with k transfers departing in [t, F(k)] are now known,
  // F(k) being the departure found for the largest count <= k, or infinity
  // when nothing with at most k transfers departs after t.
  // ----
  auto scan_first = std::optional<unixtime_t>{};
  auto const fill = [&](unixtime_t const t) {
    starts.clear();
    get_starts(SearchDir, tt, rtt, t, q.start_, q.td_start_, q.via_stops_,
               q.max_start_offset_, q.start_match_mode_,
               q.use_start_footpaths_, starts, false, q.prf_idx_,
               q.transfer_time_settings_);
    fwd.reset_arrivals();
    fwd.next_start_time();
    for (auto const& st : starts) {
      fwd.add_start(st.stop_, st.time_at_stop_);
    }
    auto ea = pareto_set<journey>{};
    fwd.execute(t, q.max_transfers_, t + step(max_tt.count()), ea);
    kFwd ? ++stats.n_execute_fwd_ : ++stats.n_execute_bwd_;

    auto found = std::array<std::optional<unixtime_t>, kMaxTransfers + 1U>{};
    for (auto const& e : ea) {
      if (!scan_first.has_value() ||
          (kFwd ? e.dest_time_ < *scan_first : e.dest_time_ > *scan_first)) {
        scan_first = e.dest_time_;
      }
      starts.clear();
      get_starts(kScanDir, tt, rtt, e.dest_time_, bq.start_, bq.td_start_,
                 bq.via_stops_, bq.max_start_offset_, bq.start_match_mode_,
                 bq.use_start_footpaths_, starts, false, bq.prf_idx_,
                 bq.transfer_time_settings_);
      bwd.reset_arrivals();
      bwd.next_start_time();
      for (auto const& st : starts) {
        bwd.add_start(st.stop_, st.time_at_stop_);
      }
      auto res = pareto_set<journey>{};
      bwd.execute(e.dest_time_, e.transfers_, e.start_time_, res);
      kFwd ? ++stats.n_execute_bwd_ : ++stats.n_execute_fwd_;
      auto const match = utl::find_if(res, [&](journey const& r) {
        return r.transfers_ == e.transfers_ && r.start_time_ == e.dest_time_;
      });
      if (match == end(res)) {
        throw utl::fail("reversed: no latest departure for transfers={}, "
                        "arrival={}",
                        e.transfers_, e.dest_time_);
      }
      found[e.transfers_] = match->dest_time_;
      add_found(bwd, journey{*match});
    }

    auto best = std::optional<unixtime_t>{};
    for (auto k = 0U; k <= kMaxTransfers; ++k) {
      if (found[k].has_value()) {
        best = found[k];
      }
      if (!is_better(frontier[k] + step(1), t)) {  // frontier reaches t
        frontier[k] = further(frontier[k], best.value_or(kInfinity));
      }
    }
  };

  // ====
  // The scan: backward runs over the arrival events at the destination in
  // ascending order on one round table, lazily, in batches sized by the
  // interval estimator. scan_to is the last scanned arrival; every journey
  // arriving up to it is known.
  // ----
  auto sq = bq;
  sq.min_connection_count_ = m;
  sq.max_interval_ = std::nullopt;
  auto scan_itv = std::optional<interval<unixtime_t>>{};
  auto scan_pos = std::size_t{0U};
  auto scan_starts = std::vector<start>{};
  auto scan_results = pareto_set<journey>{};
  auto scan_to = std::optional<unixtime_t>{};
  auto scan_exhausted = false;
  auto const covered = [&](journey const& j) {
    return scan_to.has_value() && !is_better(*scan_to, j.dest_time_);
  };
  auto const add_scan_starts = [&](interval<unixtime_t> const& iv) {
    get_starts(kScanDir, tt, rtt, iv, sq.start_, sq.td_start_, sq.via_stops_,
               sq.max_start_offset_, sq.start_match_mode_,
               sq.use_start_footpaths_, scan_starts, false, sq.prf_idx_,
               sq.transfer_time_settings_);
    std::sort(begin(scan_starts) + static_cast<std::ptrdiff_t>(scan_pos),
              end(scan_starts), [](start const& a, start const& b) {
                // the scan runs against the query direction
                return SearchDir == direction::kBackward ? b < a : a < b;
              });
  };
  // Runs the scan until pred() holds or it is exhausted; returns the number
  // of new journeys.
  auto const scan_until = [&](auto const& pred) {
    auto n_new = 0U;
    if (!scan_itv.has_value()) {
      sq.start_time_ =
          interval<unixtime_t>{*scan_first, *scan_first + duration_t{1}};
      scan_itv = interval_estimator<kScanDir>{tt, sq}.initial(
          std::get<interval<unixtime_t>>(sq.start_time_));
      scan.reset_arrivals();
      add_scan_starts(*scan_itv);
    }
    while (!pred() && !scan_exhausted && !is_timeout_reached()) {
      if (scan_pos == scan_starts.size()) {
        auto const ext = interval_estimator<kScanDir>{tt, sq}.extension(
            *scan_itv, m);
        if (ext == *scan_itv) {
          scan_exhausted = true;
          break;
        }
        scan_starts.clear();
        scan_pos = 0U;
        add_scan_starts(kScanDir == direction::kForward
                            ? interval{ext.from_, scan_itv->from_}
                            : interval{scan_itv->to_, ext.to_});
        ++stats.interval_extensions_;
        scan_itv = ext;
        continue;
      }
      auto const start_time = scan_starts[scan_pos].time_at_start_;
      scan.next_start_time();
      for (; scan_pos != scan_starts.size() &&
             scan_starts[scan_pos].time_at_start_ == start_time;
           ++scan_pos) {
        scan.add_start(scan_starts[scan_pos].stop_,
                       scan_starts[scan_pos].time_at_stop_);
      }
      scan_results.clear();
      scan.execute(start_time, q.max_transfers_, start_time - step(max_tt.count()),
                   scan_results);
      kFwd ? ++stats.n_execute_bwd_ : ++stats.n_execute_fwd_;
      scan_to = start_time;
      for (auto& j : scan_results) {
        if (add_found(scan, journey{j})) {
          ++n_new;
        }
      }
    }
    return n_new;
  };

  // ====
  // Merge.
  // ----
  auto const next_candidate = [&]() -> journey const* {
    journey const* best = nullptr;
    for (auto const& j : all) {
      if (is_reported(j)) {
        continue;
      }
      if (best == nullptr || is_better(j.start_time_, best->start_time_) ||
          (j.start_time_ == best->start_time_ &&
           j.transfers_ < best->transfers_)) {
        best = &j;
      }
    }
    return best;
  };

  s_state.results_.clear();
  fill(bound);  // the seed: Pong's first iteration from the query time
  if (!scan_first.has_value()) {
    return finish_dep(s_state, stats, itv, processing_start_time, fwd, bwd,
                      scan);
  }
  scan_until([&]() { return all.size() >= m; });

  auto n_reported = 0U;
  auto last_reported = std::optional<unixtime_t>{};
  while (!is_timeout_reached()) {
    auto const* cand_ptr = next_candidate();
    if (cand_ptr == nullptr) {
      if (!scan_exhausted) {
        scan_until([&]() { return next_candidate() != nullptr; });
        continue;
      }
      // Scan done: continue like Pong from the least advanced frontier.
      auto k_min = 0U;
      for (auto k = 1U; k <= kMaxTransfers; ++k) {
        if (is_better(frontier[k], frontier[k_min])) {
          k_min = k;
        }
      }
      if (frontier[k_min] == kInfinity) {
        break;
      }
      fill(frontier[k_min] + step(1));
      continue;
    }
    auto const cand = *cand_ptr;  // a fill or scan reallocates `all`
    if (n_reported >= n && cand.start_time_ != *last_reported) {
      break;
    }
    // Which transfer counts could still hide a journey departing before the
    // candidate? One arriving inside the scanned range: only fewer
    // transfers, since more transfers and an earlier departure imply an
    // earlier arrival (else the candidate dominates it), which the scan has
    // seen. One arriving beyond the scanned range: every other count.
    auto k_fill = std::optional<unsigned>{};
    for (auto k = 0U; k <= kMaxTransfers; ++k) {
      if (covered(cand) && k >= cand.transfers_) {
        break;
      }
      if (k == cand.transfers_) {
        continue;
      }
      if (is_better(frontier[k], cand.start_time_) &&
          (!k_fill.has_value() || is_better(frontier[k], frontier[*k_fill]))) {
        k_fill = k;
      }
    }
    if (k_fill.has_value()) {
      fill(frontier[*k_fill] + step(1));
      continue;
    }
    reported.emplace_back(key(cand));
    last_reported = cand.start_time_;
    if (reportable(cand)) {
      ++n_reported;
      s_state.results_.add(journey{cand});
    }
  }

  auto result_itv = itv;
  if (last_reported.has_value()) {
    if constexpr (kFwd) {
      result_itv.to_ = std::max(itv.to_, *last_reported + duration_t{1});
    } else {
      result_itv.from_ = std::min(itv.from_, *last_reported);
    }
  }
  finalize_results<SearchDir, Vias>(tt, rtt, q, result_itv, s_state.results_);
  return finish_dep(s_state, stats, result_itv, processing_start_time, fwd,
                    bwd, scan);
}

template <direction SearchDir, via_offset_t Vias>
routing_result reversed_with_vias(timetable const& tt,
                                  rt_timetable const* rtt,
                                  search_state& s_state,
                                  raptor_state& r_state,
                                  query q,
                                  std::optional<std::chrono::seconds> timeout,
                                  reversed_order const order) {
  if (order == reversed_order::kDeparture) {
    if (rtt == nullptr) {
      return reversed_dep<SearchDir, false, Vias>(tt, rtt, s_state, r_state,
                                                  std::move(q), timeout);
    } else {
      return reversed_dep<SearchDir, true, Vias>(tt, rtt, s_state, r_state,
                                                 std::move(q), timeout);
    }
  }
  if (rtt == nullptr) {
    return reversed<SearchDir, false, Vias>(tt, rtt, s_state, r_state,
                                            std::move(q), timeout);
  } else {
    return reversed<SearchDir, true, Vias>(tt, rtt, s_state, r_state,
                                           std::move(q), timeout);
  }
}

template <direction SearchDir>
routing_result reversed_with_dir(timetable const& tt,
                                 rt_timetable const* rtt,
                                 search_state& s_state,
                                 raptor_state& r_state,
                                 query q,
                                 std::optional<std::chrono::seconds> timeout,
                                 reversed_order const order) {
  q.sanitize(tt);
  static_assert(kMaxVias == 2, "reversed.cc needs to be adjusted for kMaxVias");
  switch (q.via_stops_.size()) {
    case 0:
      return reversed_with_vias<SearchDir, 0>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout, order);
    case 1:
      return reversed_with_vias<SearchDir, 1>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout, order);
    case 2:
      return reversed_with_vias<SearchDir, 2>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout, order);
  }
  throw utl::fail("{} vias not supported (max={})", q.via_stops_.size(),
                  kMaxVias);
}

}  // namespace

template <typename AlgoState>
routing_result reversed_search(timetable const& tt,
                               rt_timetable const* rtt,
                               search_state& s_state,
                               AlgoState& r_state,
                               query q,
                               direction const search_dir,
                               std::optional<std::chrono::seconds> timeout,
                               reversed_order const order) {
  static_assert(std::is_same_v<AlgoState, raptor_state>,
                "reversed_search runs on the CPU only");
  if (search_dir == direction::kForward) {
    return reversed_with_dir<direction::kForward>(
        tt, rtt, s_state, r_state, std::move(q), timeout, order);
  } else {
    return reversed_with_dir<direction::kBackward>(
        tt, rtt, s_state, r_state, std::move(q), timeout, order);
  }
}

template routing_result reversed_search(timetable const&,
                                        rt_timetable const*,
                                        search_state&,
                                        raptor_state&,
                                        query,
                                        direction,
                                        std::optional<std::chrono::seconds>,
                                        reversed_order);

}  // namespace nigiri::routing
