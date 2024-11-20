#include "nigiri/routing/raptor_n_to_all.h"

#include "nigiri/routing/raptor_search.h"

namespace nigiri::routing {

void raptor_n_to_all(timetable const& tt,
                     rt_timetable const* rtt,
                     search_state& s_state,
                     raptor_state& r_state,
                     std::vector<std::vector<unixtime_t>>& times,
                     query q,
                     direction search_dir,
                     std::optional<std::chrono::seconds> const timeout) {
  raptor_search(tt, rtt, s_state, r_state, q, search_dir, timeout);
  auto const round_times = r_state.get_round_times<0>();
  auto const base = base_date(tt, base_day_idx(tt, q));
  times.resize(tt.n_locations());
  for (auto l = 0U; l != tt.n_locations(); ++l) {
    times[l].clear();
    for (auto i = 0U; i != kMaxTransfers + 1U; ++i) {
      times[l].emplace_back(delta_to_unix(base, delta_t{round_times[i][l][0]}));
    }
  }
}

}  // namespace nigiri::routing