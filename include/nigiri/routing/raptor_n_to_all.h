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

void raptor_n_to_all(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    raptor_state& r_state,
    query q,
    direction search_dir,
    std::vector<std::vector<unixtime_t>>& times,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing