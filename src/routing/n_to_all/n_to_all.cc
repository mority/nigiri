#include "nigiri/routing/n_to_all/n_to_all.h"

namespace nigiri::routing {

template <direction SearchDir>
void raptor_n_to_all(timetable const& tt,
                     rt_timetable const* rtt,
                     search_state& s_state,
                     raptor_state& r_state,
                     std::vector<std::vector<n_to_all_label>>& labels,
                     query q,
                     std::optional<std::chrono::seconds> timeout) {
  raptor_search(tt, rtt, s_state, r_state, q, SearchDir, timeout);
  auto const round_times = r_state.get_round_times<0>();
  auto const b = base_date(tt, base_day_idx(tt, q));
  labels.resize(tt.n_locations());
  for (auto l = 0U; l != tt.n_locations(); ++l) {
    labels[l].clear();
    for (auto i = 0U; i != kMaxTransfers + 1U; ++i) {
      auto const delta_to_unix_with_invalids = [&](auto&& base, auto&& delta) {
        return delta == kInvalidDelta<SearchDir> ? kNToAllUnreached
                                                 : delta_to_unix(base, delta);
      };
      labels[l].emplace_back(
          delta_to_unix_with_invalids(b, delta_t{round_times[i][l][0]}));
    }
  }
}

template void raptor_n_to_all<direction::kForward>(
    timetable const&,
    rt_timetable const*,
    search_state&,
    raptor_state&,
    std::vector<std::vector<n_to_all_label>>&,
    query,
    std::optional<std::chrono::seconds> = std::nullopt);
template void raptor_n_to_all<direction::kBackward>(
    timetable const&,
    rt_timetable const*,
    search_state&,
    raptor_state&,
    std::vector<std::vector<n_to_all_label>>&,
    query,
    std::optional<std::chrono::seconds> = std::nullopt);

}  // namespace nigiri::routing