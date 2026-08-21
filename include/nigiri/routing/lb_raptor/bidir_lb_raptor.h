#pragma once

#include <tuple>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

struct bidir_lb_raptor_stats {
  void reset() {
    pattern_reconstructions_ = 0UL;
    pattern_repetitions_ = 0UL;
    truncated_patterns_ = 0UL;
    unrealizable_patterns_ = 0UL;
    pruned_meetpoints_ = 0UL;
  }

  std::uint64_t pattern_reconstructions_;
  std::uint64_t pattern_repetitions_;
  // reconstruct() did not reach the start/destination
  std::uint64_t truncated_patterns_;
  // no journey could be built for a complete pattern
  std::uint64_t unrealizable_patterns_;
  // dropped by `max_patterns_per_round_` before being reconstructed
  std::uint64_t pruned_meetpoints_;
};

struct bidir_lb_raptor {
  void execute(timetable const&,
               rt_timetable const*,
               query const&,
               bool arrive_by = false);

private:
  void reset(unsigned n_locations, unsigned n_lb_routes);

  template <direction SearchDir>
  void init(timetable const&, query const&, bool arrive_by);

  template <direction SearchDir>
  bool run(timetable const&, query const&, unsigned k);

  template <direction SearchDir>
  void meetpoints_to_patterns(timetable const&,
                              rt_timetable const*,
                              query const&,
                              unsigned k,
                              bool arrive_by);

  // Returns false if the terminal was not reached, i.e. the pattern in
  // `current_pattern_` is truncated and therefore unusable.
  template <direction SearchDir>
  [[nodiscard]] bool reconstruct(timetable const&,
                                 query const&,
                                 location_idx_t,
                                 unsigned k_start);

public:
  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      fwd_round_times_;
  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      bwd_round_times_;
  vector_map<location_idx_t, std::uint16_t> tmp_;
  bitvec fwd_station_mark_;
  bitvec bwd_station_mark_;
  bitvec prev_station_mark_;
  bitvec fwd_reached_;
  bitvec bwd_reached_;
  bitvec is_start_;
  bitvec is_dest_;
  bitvec lb_route_mark_;
  bitvec is_src_;
  bitvec is_dst_;
  // Lower bound on what a journey through a meetpoint can achieve. Both parts
  // follow from the round tables alone, so meetpoints can be ranked *before*
  // anything is reconstructed: the forward search needs `fwd_k` transports to
  // reach the meetpoint and the backward search `bwd_k`, and the two labels
  // add up to the travel time through it.
  struct scored_meetpoint {
    friend bool operator<(scored_meetpoint const& a, scored_meetpoint const& b) {
      return std::tie(a.transfers_, a.travel_time_lb_) <
             std::tie(b.transfers_, b.travel_time_lb_);
    }

    location_idx_t l_;
    std::uint8_t transfers_;
    std::uint32_t travel_time_lb_;
  };

  // Maximum number of meetpoints per round that are turned into patterns, best
  // score first. On the Berlin timetable 50 keeps the top-ranked journeys
  // unchanged while cutting the runtime by ~6x; raise it to trade time for a
  // longer tail of alternatives, or set it to `unsigned` max for no limit.
  unsigned max_patterns_per_round_{50U};

  std::map<location_idx_t, std::uint16_t> min_;
  std::vector<location_idx_t> meetpoints_;
  std::vector<scored_meetpoint> scored_meetpoints_;
  std::vector<location_idx_t> current_pattern_;
  std::set<std::array<location_idx_t, kMaxTransfers + 2U>> patterns_;
  // All alternatives, one per distinct pattern - deliberately *not* a
  // pareto_set: a journey that is dominated on (departure, arrival, transfers)
  // may still be interesting because of the pattern it uses.
  std::vector<journey> journeys_;
  bidir_lb_raptor_stats stats_;
};

}  // namespace nigiri::routing