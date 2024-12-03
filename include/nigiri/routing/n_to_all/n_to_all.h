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

struct n_to_all_label {
  duration_t travel_time_;
  std::uint16_t n_transfers_;
  location_idx_t source_;
};

struct n_to_all_state {
  std::vector<std::vector<n_to_all_label>> labels_;
};

template <direction SearchDir>
void raptor_n_to_all(timetable const&,
                     rt_timetable const*,
                     search_state&,
                     raptor_state&,
                     std::vector<std::vector<n_to_all_label>>&,
                     query,
                     std::optional<std::chrono::seconds> = std::nullopt);

}  // namespace nigiri::routing