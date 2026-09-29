#pragma once

#include <vector>

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {

struct query;
struct search_state;
struct raptor_state;
struct journey;

template <direction SearchDir>
void reconstruct_journey(timetable const&,
                         rt_timetable const*,
                         query const&,
                         raptor_state const&,
                         journey&,
                         date::sys_days const base,
                         day_idx_t const base_day_idx);

// Reconstructs further journeys with the criteria of the (reconstructed)
// journey j: for each transit leg of j up to n - 1 journeys that take
// another option at this leg. Legs between this leg and the destination of
// the search are those of j, legs towards the start are reconstructed from
// the option taken (first fit). All journeys have different sequences of
// trips.
struct alternatives_stats {
  std::uint64_t n_attempts_{0U};  // reconstructions started
  std::uint64_t n_failed_{0U};  // ... that did not lead to a journey
  std::uint64_t n_duplicates_{0U};  // ... with a known sequence of trips
  std::uint64_t n_capped_{0U};  // results that hit a limit
  // one leg at a time: smallest number of combinations passed over that led
  // to a new journey, 0 = no alternative
  std::uint64_t min_skip_{0U};
};

template <direction SearchDir>
alternatives_stats reconstruct_alternatives(timetable const&,
                                            rt_timetable const*,
                                            query const&,
                                            raptor_state const&,
                                            journey const& j,
                                            unsigned n,
                                            std::vector<journey>& alternatives,
                                            date::sys_days const base,
                                            day_idx_t const base_day_idx);

void optimize_footpaths(timetable const&,
                        rt_timetable const*,
                        query const&,
                        journey&);

void specify_td_offsets(query const&, journey&);

}  // namespace nigiri::routing
