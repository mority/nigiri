#pragma once

#include "nigiri/routing/journey.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"

#include "nigiri/routing/debug.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/tripbased/preprocessing/preprocessor.h"
#include "nigiri/routing/tripbased/transfer_set.h"

namespace nigiri::test {

pareto_set<routing::journey> tripbased_search(timetable& tt, routing::query q) {
  using algo_t = routing::tripbased::query_engine;
  using algo_state_t = routing::tripbased::query_state;

  static auto search_state = routing::search_state{};
  routing::tripbased::transfer_set ts;
  build_transfer_set(tt, ts);
  auto algo_state = algo_state_t{tt, ts};

  return *(routing::search<direction::kForward, algo_t>{
      tt, nullptr, search_state, algo_state, std::move(q)}
               .execute()
               .journeys_);
}

pareto_set<routing::journey> tripbased_search(timetable& tt,
                                              std::string_view from,
                                              std::string_view to,
                                              routing::start_time_t time) {
  auto const src = source_idx_t{0};
  auto q = routing::query{
      .start_time_ = time,
      .start_ = {{tt.locations_.location_id_to_idx_.at({from, src}), 0_minutes,
                  0U}},
      .destination_ = {
          {tt.locations_.location_id_to_idx_.at({to, src}), 0_minutes, 0U}}};
  return tripbased_search(tt, std::move(q));
}

pareto_set<routing::journey> tripbased_intermodal_search(
    timetable& tt,
    std::vector<routing::offset> start,
    std::vector<routing::offset> destination,
    interval<unixtime_t> interval,
    std::uint8_t const min_connection_count = 0U,
    bool const extend_interval_earlier = false,
    bool const extend_interval_later = false) {
  auto q = routing::query{
      .start_time_ = interval,
      .start_match_mode_ = routing::location_match_mode::kIntermodal,
      .dest_match_mode_ = routing::location_match_mode::kIntermodal,
      .start_ = std::move(start),
      .destination_ = std::move(destination),
      .min_connection_count_ = min_connection_count,
      .extend_interval_earlier_ = extend_interval_earlier,
      .extend_interval_later_ = extend_interval_later};
  return tripbased_search(tt, std::move(q));
}

}  // namespace nigiri::test