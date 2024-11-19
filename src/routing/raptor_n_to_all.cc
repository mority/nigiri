#include "nigiri/routing/raptor_n_to_all.h"

#include "nigiri/routing/raptor_search.h"

namespace nigiri::routing {

std::vector<std::vector<duration_t>> get_travel_times(
    raptor_state const& r_state) {
  auto travel_times = std::vector<std::vector<duration_t>>{};

  return travel_times;
}

n_to_all_result raptor_n_to_all(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    raptor_state& r_state,
    query q,
    direction search_dir,
    std::optional<std::chrono::seconds> const timeout) {
  return n_to_all_result{.result_ = raptor_search(tt, rtt, s_state, r_state, q,
                                                  search_dir, timeout),
                         .travel_times_ = get_travel_times(r_state)};
}

}  // namespace nigiri::routing