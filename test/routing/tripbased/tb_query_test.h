#pragma once

#include "nigiri/routing/journey.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"

#include "nigiri/routing/debug.h"
#include "nigiri/routing/pareto_set.h"

namespace nigiri::test {

pareto_set<routing::journey> tripbased_search(timetable& tt, routing::query q) {
  using algo_t = routing::tripbased::tb_query;
  using algo_state_t = routing::tripbased::tb_query_state;

  static auto search_state = routing::search_state{};
  routing::tripbased::tb_preprocessing tbp{tt};
  tbp.build_transfer_set();
  day_idx_t base =
      tt.day_idx_mam(holds_alternative<unixtime_t>(q.start_time_)
                         ? get<unixtime_t>(q.start_time_)
                         : get<interval<unixtime_t>>(q.start_time_).to_)
          .first;
  auto algo_state = algo_state_t{tbp, base};

  return *(routing::search<direction::kForward, algo_t>{
      tt, search_state, algo_state, std::move(q)}
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

}  // namespace nigiri::test