#pragma once

#include "nigiri/routing/raptor_search.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
enum class direction;
}  // namespace nigiri

namespace nigiri::routing {

struct search_state;
struct raptor_state;
struct query;

struct n_to_all_result {
  routing_result<raptor_stats> result_;
  std::vector<std::vector<duration_t>> travel_times_;
};

n_to_all_result raptor_n_to_all(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    raptor_state& r_state,
    query q,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing