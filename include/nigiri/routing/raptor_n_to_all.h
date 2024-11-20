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

static constexpr auto const kNToAllUnreached = unixtime_t{duration_t{0}};

template <direction SearchDir>
void raptor_n_to_all(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    raptor_state& r_state,
    std::vector<std::vector<unixtime_t>>& times,
    query q,
    std::optional<std::chrono::seconds> timeout = std::nullopt) {
  raptor_search(tt, rtt, s_state, r_state, q, SearchDir, timeout);
  auto const round_times = r_state.get_round_times<0>();
  auto const b = base_date(tt, base_day_idx(tt, q));
  times.resize(tt.n_locations());
  for (auto l = 0U; l != tt.n_locations(); ++l) {
    times[l].clear();
    for (auto i = 0U; i != kMaxTransfers + 1U; ++i) {
      auto const delta_to_unix_with_invalids = [&](auto&& base, auto&& delta) {
        return delta == kInvalidDelta<SearchDir> ? kNToAllUnreached
                                                 : delta_to_unix(base, delta);
      };
      times[l].emplace_back(
          delta_to_unix_with_invalids(b, delta_t{round_times[i][l][0]}));
    }
  }
}

}  // namespace nigiri::routing