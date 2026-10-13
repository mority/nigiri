#include "nigiri/routing/raptor/reversed.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>

#include "utl/erase_if.h"
#include "utl/helpers/algorithm.h"
#include "utl/nwise.h"
#include "utl/verify.h"

#include "nigiri/routing/direct.h"
#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/rt/frun.h"
#include "nigiri/special_stations.h"

namespace nigiri::routing {

namespace {

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
  auto const swap_special = [](location_idx_t const l) -> location_idx_t {
    switch (to_idx(l)) {
      case to_idx(get_special_station(special_station::kStart)):
        return get_special_station(special_station::kEnd);
      case to_idx(get_special_station(special_station::kEnd)):
        return get_special_station(special_station::kStart);
      default: return l;
    }
  };
  for (auto& j : s_state.results_) {
    std::swap(j.start_time_, j.dest_time_);
    if (!j.legs_.empty()) {
      j.legs_.front().from_ = swap_special(j.legs_.front().from_);
      j.legs_.back().to_ = swap_special(j.legs_.back().to_);
    }
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

  enrich_with_slow_direct<SearchDir>(tt, rtt, q, result_itv, s_state.results_);

  utl::sort(s_state.results_, [](journey const& a, journey const& b) {
    return std::tuple{a.start_time_, a.transfers_} <
           std::tuple{b.start_time_, b.transfers_};
  });

  if constexpr (kFwd && Vias == 0U) {
    // Backward reconstruction yields the latest possible intermediate legs;
    // move them to the earliest alternative like pong_search does.
    for (auto& j : s_state.results_) {
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

  return finish(result_itv);
}

template <direction SearchDir, via_offset_t Vias>
routing_result reversed_with_vias(timetable const& tt,
                                  rt_timetable const* rtt,
                                  search_state& s_state,
                                  raptor_state& r_state,
                                  query q,
                                  std::optional<std::chrono::seconds> timeout) {
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
                                 std::optional<std::chrono::seconds> timeout) {
  q.sanitize(tt);
  static_assert(kMaxVias == 2, "reversed.cc needs to be adjusted for kMaxVias");
  switch (q.via_stops_.size()) {
    case 0:
      return reversed_with_vias<SearchDir, 0>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout);
    case 1:
      return reversed_with_vias<SearchDir, 1>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout);
    case 2:
      return reversed_with_vias<SearchDir, 2>(tt, rtt, s_state, r_state,
                                              std::move(q), timeout);
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
                               std::optional<std::chrono::seconds> timeout) {
  static_assert(std::is_same_v<AlgoState, raptor_state>,
                "reversed_search runs on the CPU only");
  if (search_dir == direction::kForward) {
    return reversed_with_dir<direction::kForward>(tt, rtt, s_state, r_state,
                                                  std::move(q), timeout);
  } else {
    return reversed_with_dir<direction::kBackward>(tt, rtt, s_state, r_state,
                                                   std::move(q), timeout);
  }
}

template routing_result reversed_search(timetable const&,
                                        rt_timetable const*,
                                        search_state&,
                                        raptor_state&,
                                        query,
                                        direction,
                                        std::optional<std::chrono::seconds>);

}  // namespace nigiri::routing
