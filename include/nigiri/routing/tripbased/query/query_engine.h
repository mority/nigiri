#pragma once

#include <cinttypes>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "query_state.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing::tripbased {

struct query_stats {};

struct query_engine {
  using algo_state_t = query_state;
  using algo_stats_t = query_stats;

  static constexpr bool kUseLowerBounds = false;

  query_engine(timetable const& tt,
               rt_timetable const* rtt,
               query_state& state,
               std::vector<bool>& is_dest,
               std::vector<std::uint16_t>& dist_to_dest,
               std::vector<std::uint16_t>& lb,
               day_idx_t const base);

  algo_stats_t get_stats() const { return stats_; }

  algo_state_t& get_state() { return state_; }

  void reset_arrivals() {
#ifndef NDEBUG
    TBDL << "reset_arrivals\n";
#endif
    state_.r_.reset();
    std::fill(state_.t_min_.begin(), state_.t_min_.end(), unixtime_t::max());
  }

  void next_start_time() {
#ifndef NDEBUG
    TBDL << "next_start_time\n";
#endif
    state_.q_n_.reset(base_);
    state_.query_starts_.clear();
  }

  void add_start(location_idx_t const l, unixtime_t const t) {
#ifndef NDEBUG
    TBDL << "add_start: " << tt_.locations_.names_.at(l).view() << ", "
         << dhhmm(unix_tt(tt_, t)) << "\n";
#endif
    state_.query_starts_.emplace_back(l, t);
  }

  void execute(unixtime_t const start_time,
               std::uint8_t const max_transfers,
               unixtime_t const worst_time_at_dest,
               pareto_set<journey>& results);

  void reconstruct(query const& q, journey& j) const;

private:
  void handle_start(query_start const&);

  void handle_start_footpath(std::int32_t const,
                             std::int32_t const,
                             footpath const);

#ifdef TB_CACHE_PRESSURE_REDUCTION
#if !defined(TB_MIN_WALK) && !defined(TB_TRANSFER_CLASS)
  void seg_dest(unixtime_t const start_time,
                pareto_set<journey>& results,
                unixtime_t worst_time_at_dest,
                std::uint8_t const n,
                transport_segment& seg);
#else
  void seg_dest(unixtime_t const start_time,
                pareto_set<journey>& results,
                std::uint8_t const n,
                transport_segment& seg);
#endif

#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
  void seg_prune(unixtime_t const start_time,
                 unixtime_t const worst_time_at_dest,
                 pareto_set<journey>& results,
                 std::uint8_t const n,
                 transport_segment& seg);
#else
  void seg_prune(unixtime_t const worst_time_at_dest,
                 std::uint8_t const n,
                 transport_segment& seg);
#endif

  void seg_transfers(std::uint8_t const n, queue_idx_t const q_cur);

#else

  void handle_segment(unixtime_t const start_time,
                      unixtime_t const worst_time_at_dest,
                      pareto_set<journey>& results,
                      std::uint8_t const n,
                      queue_idx_t const q_cur);
#endif
  struct journey_end {
    journey_end(queue_idx_t const seg_idx,
                route_dest const& le,
                location_idx_t const le_location,
                location_idx_t const last_location)
        : seg_idx_(seg_idx),
          le_(le),
          le_location_(le_location),
          last_location_(last_location) {}

    // the last transport segment of the journey
    queue_idx_t seg_idx_;
    // the l_entry for the destination of the journey
    route_dest le_;
    // the location idx of the l_entry
    location_idx_t le_location_;
    // the reconstructed destination of the journey
    location_idx_t last_location_;
  };

  std::optional<journey_end> reconstruct_journey_end(query const& q,
                                                     journey const& j) const;

  void add_final_footpath(query const& q,
                          journey& j,
                          journey_end const& je) const;

  void add_segment_leg(journey& j, transport_segment const& seg) const;

  // reconstruct the transfer from the given segment to the last journey leg
  // returns the stop idx at which the segment is exited
  std::optional<unsigned> reconstruct_transfer(
      journey& j, transport_segment const& seg) const;

  void add_initial_footpath(query const& q, journey& j) const;

  bool is_start_location(query const&, location_idx_t const) const;

  timetable const& tt_;
  rt_timetable const* rtt_;
  query_state& state_;
  std::vector<bool>& is_dest_;
  std::vector<std::uint16_t>& dist_to_dest_;
  std::vector<std::uint16_t>& lb_;
  day_idx_t const base_;
  query_stats stats_;
};

}  // namespace nigiri::routing::tripbased