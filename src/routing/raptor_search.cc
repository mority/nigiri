#include "nigiri/routing/raptor_search.h"

#include <string>
#include <string_view>
#include <utility>

#include "date/date.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include "utl/overloaded.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "nigiri/get_otel_tracer.h"
#include "nigiri/routing/gpu/raptor.h"
#include "nigiri/routing/query.h"

namespace nigiri::routing {

namespace {

// True if real-time data could possibly change this query's answer.
//
// The search only ever looks at events inside the window it can reach: forward
// from the earliest start until max_travel_time later, backward from the latest
// arrival until max_travel_time earlier. If that window does not touch the span
// the feed actually covers, every rt transport the search would mark is out of
// range -- it walks their stop sequences, boards nothing, and returns exactly
// the scheduled answer, having paid for the whole real-time scan.
//
// Conservative on purpose: when in doubt this says yes, because a wrong "no"
// silently drops real-time from the result while a wrong "yes" only costs time.
template <direction SearchDir>
bool rt_in_reach(rt_timetable const* rtt, query const& q) {
  if (rtt == nullptr) {
    return false;
  }

  // Time-dependent footpaths live in the rt timetable as well and are not tied
  // to transport events at all, so their presence alone makes it relevant
  // whatever the query's time window is.
  if ((SearchDir == direction::kForward
           ? rtt->has_td_footpaths_out_
           : rtt->has_td_footpaths_in_)[q.prf_idx_]
          .any()) {
    return true;
  }

  if (!rtt->has_rt_events()) {
    return false;
  }

  auto const start = std::visit(
      utl::overloaded{[](unixtime_t const t) { return interval{t, t}; },
                      [](interval<unixtime_t> const i) { return i; }},
      q.start_time_);
  auto const covered = rtt->event_interval();

  // `to_` of a start interval is exclusive, and an ontrip query has from_==to_,
  // so compare against the last instant the search can start from.
  auto const last_start =
      start.to_ > start.from_ ? start.to_ - duration_t{1} : start.from_;

  if constexpr (SearchDir == direction::kForward) {
    return start.from_ < covered.to_ &&
           last_start + q.max_travel_time_ >= covered.from_;
  } else {
    return last_start >= covered.from_ &&
           start.from_ - q.max_travel_time_ < covered.to_;
  }
}

template <direction SearchDir, via_offset_t Vias, typename AlgoState>
routing_result raptor_search_with_vias(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    AlgoState& r_state,
    query q,
    std::optional<std::chrono::seconds> const timeout) {
  if (rtt == nullptr) {
    using algo_t = std::conditional_t<
        std::is_same_v<AlgoState, gpu::gpu_raptor_state>,
        gpu::gpu_raptor<SearchDir, false>,
        raptor<SearchDir, false, Vias, search_mode::kOneToOne>>;
    return search<SearchDir, algo_t>{tt,      rtt,          s_state,
                                     r_state, std::move(q), timeout}
        .execute();
  } else {
    using algo_t = std::conditional_t<
        std::is_same_v<AlgoState, gpu::gpu_raptor_state>,
        gpu::gpu_raptor<SearchDir, false>,
        raptor<SearchDir, true, Vias, search_mode::kOneToOne>>;
    return search<SearchDir, algo_t>{tt,      rtt,          s_state,
                                     r_state, std::move(q), timeout}
        .execute();
  }
}

template <direction SearchDir, typename AlgoState>
routing_result raptor_search_with_dir(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    AlgoState& algo_state,
    query q,
    std::optional<std::chrono::seconds> const timeout) {
  q.sanitize(tt);

  // Nothing the real-time data says can reach this query: route it on the
  // scheduled timetable, which is the same answer for less work.
  if (rtt != nullptr && !rt_in_reach<SearchDir>(rtt, q)) {
    rtt = nullptr;
  }

  utl::verify(q.via_stops_.size() <= kMaxVias,
              "too many via stops: {}, limit: {}", q.via_stops_.size(),
              kMaxVias);

  static_assert(kMaxVias == 2,
                "raptor_search.cc needs to be adjusted for kMaxVias");

  switch (q.via_stops_.size()) {
    case 0:
      return raptor_search_with_vias<SearchDir, 0>(tt, rtt, s_state, algo_state,
                                                   std::move(q), timeout);
    case 1:
      return raptor_search_with_vias<SearchDir, 1>(tt, rtt, s_state, algo_state,
                                                   std::move(q), timeout);
    case 2:
      return raptor_search_with_vias<SearchDir, 2>(tt, rtt, s_state, algo_state,
                                                   std::move(q), timeout);
  }
  std::unreachable();
}

std::string_view location_match_mode_str(location_match_mode const mode) {
  using namespace std::literals;
  switch (mode) {
    case location_match_mode::kExact: return "exact"sv;
    case location_match_mode::kOnlyChildren: return "only_children"sv;
    case location_match_mode::kEquivalent: return "equivalent"sv;
    case location_match_mode::kIntermodal: return "intermodal"sv;
  }
  std::unreachable();
}

}  // namespace

template <typename AlgoState>
routing_result raptor_search(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    AlgoState& algo_state,
    query q,
    direction const search_dir,
    std::optional<std::chrono::seconds> const timeout) {
  auto span = get_otel_tracer()->StartSpan("raptor_search");
  auto scope = opentelemetry::trace::Scope{span};
  if (span->IsRecording()) {
    std::visit(utl::overloaded{
                   [&](interval<unixtime_t> const& interval) {
                     span->SetAttribute("nigiri.query.start_time_interval.from",
                                        date::format("%FT%RZ", interval.from_));
                     span->SetAttribute("nigiri.query.start_time_interval.to",
                                        date::format("%FT%RZ", interval.to_));
                   },
                   [&](unixtime_t const& t) {
                     span->SetAttribute("nigiri.query.start_time",
                                        date::format("%FT%RZ", t));
                   }},
               q.start_time_);
    span->SetAttribute("nigiri.query.start_match_mode",
                       location_match_mode_str(q.start_match_mode_));
    span->SetAttribute("nigiri.query.destination_match_mode",
                       location_match_mode_str(q.dest_match_mode_));
    span->SetAttribute("nigiri.query.use_start_footpaths",
                       q.use_start_footpaths_);
    span->SetAttribute("nigiri.query.start_count", q.start_.size());
    span->SetAttribute("nigiri.query.destination_count", q.destination_.size());
    span->SetAttribute("nigiri.query.td_start_count", q.td_start_.size());
    span->SetAttribute("nigiri.query.td_destination_count", q.td_dest_.size());
    span->SetAttribute("nigiri.query.max_start_offset",
                       q.max_start_offset_.count());
    span->SetAttribute("nigiri.query.max_transfers", q.max_transfers_);
    span->SetAttribute("nigiri.query.min_connection_count",
                       q.min_connection_count_);
    span->SetAttribute("nigiri.query.extend_interval_earlier",
                       q.extend_interval_earlier_);
    span->SetAttribute("nigiri.query.extend_interval_later",
                       q.extend_interval_later_);
    span->SetAttribute("nigiri.query.prf_idx", q.prf_idx_);
    span->SetAttribute("nigiri.query.allowed_classes", q.allowed_claszes_);
    span->SetAttribute("nigiri.query.require_bike_transport",
                       q.require_bike_transport_);
    span->SetAttribute("nigiri.query.transfer_time_settings.default",
                       q.transfer_time_settings_.default_);
    span->SetAttribute("nigiri.query.via_stops_count", q.via_stops_.size());
    span->SetAttribute(
        "nigiri.query.search_direction",
        search_dir == direction::kForward ? "forward" : "backward");
    if (timeout) {
      span->SetAttribute("nigiri.query.timeout", timeout.value().count());
    }
  }

  if (search_dir == direction::kForward) {
    return raptor_search_with_dir<direction::kForward>(
        tt, rtt, s_state, algo_state, std::move(q), timeout);
  } else {
    return raptor_search_with_dir<direction::kBackward>(
        tt, rtt, s_state, algo_state, std::move(q), timeout);
  }
}

template routing_result raptor_search(timetable const&,
                                      rt_timetable const*,
                                      search_state&,
                                      raptor_state&,
                                      query,
                                      direction,
                                      std::optional<std::chrono::seconds>);

#if defined(NIGIRI_CUDA)
template routing_result raptor_search(timetable const&,
                                      rt_timetable const*,
                                      search_state&,
                                      gpu::gpu_raptor_state&,
                                      query,
                                      direction,
                                      std::optional<std::chrono::seconds>);
#endif

}  // namespace nigiri::routing
