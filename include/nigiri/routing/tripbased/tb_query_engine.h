#pragma once

#include <cinttypes>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/tripbased/tb_query_state.h"

#define TRANSFERRED_FROM_NULL std::numeric_limits<std::uint32_t>::max()

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing::tripbased {

struct tb_query_stats {};

struct tb_query_engine {
  using algo_state_t = tb_query_state;
  using algo_stats_t = tb_query_stats;

  static constexpr bool kUseLowerBounds = false;

  tb_query_engine(timetable const& tt,
                  tb_query_state& state,
                  std::vector<bool>& is_dest,
                  std::vector<std::uint16_t>& dist_to_dest,
                  std::vector<std::uint16_t>& lb,
                  day_idx_t const base)
      : tt_{tt},
        state_{state},
        is_dest_{is_dest},
        dist_to_dest_{dist_to_dest},
        lb_{lb},
        base_{base} {

    // init l_, i.e., routes that reach the destination at certain stop idx
    if (dist_to_dest.empty()) {
      // create l_entries for station-to-station query
      for (location_idx_t dest{0U}; dest != location_idx_t{is_dest_.size()};
           ++dest) {
        if (is_dest_[dest.v_]) {
          // fill l_
          auto create_l_entry = [this](footpath const& fp) {
            // iterate routes serving source of footpath
            for (auto const route_idx : tt_.location_routes_[fp.target()]) {
              // iterate stop sequence of route
              for (std::uint16_t stop_idx{1U};
                   stop_idx < tt_.route_location_seq_[route_idx].size();
                   ++stop_idx) {
                auto const location_idx =
                    stop{tt_.route_location_seq_[route_idx][stop_idx]}
                        .location_idx();
                if (location_idx == fp.target_) {
                  state_.l_.emplace_back(route_idx, stop_idx, fp.duration());
                }
              }
            }
          };
          // virtual reflexive incoming footpath
          create_l_entry(footpath{dest, duration_t{0U}});
          // iterate incoming footpaths of target location
          for (auto const fp : tt_.locations_.footpaths_in_[dest]) {
            create_l_entry(fp);
          }
        }
      }
    } else {
      // create l_entries for coord-to-coord query
      for (location_idx_t dest{0U}; dest != location_idx_t{dist_to_dest.size()};
           ++dest) {
        if (dist_to_dest[dest.v_] !=
            std::numeric_limits<std::uint16_t>::max()) {
          // fill l_
          auto create_l_entry = [this, &dest](footpath const& fp) {
            // iterate routes serving source of footpath
            for (auto const route_idx : tt_.location_routes_[fp.target()]) {
              // iterate stop sequence of route
              for (std::uint16_t stop_idx{1U};
                   stop_idx < tt_.route_location_seq_[route_idx].size();
                   ++stop_idx) {
                auto const location_idx =
                    stop{tt_.route_location_seq_[route_idx][stop_idx]}
                        .location_idx();
                if (location_idx == fp.target_) {
                  state_.l_.emplace_back(
                      route_idx, stop_idx,
                      fp.duration() + duration_t{dist_to_dest_[dest.v_]});
                }
              }
            }
          };
          // virtual reflexive incoming footpath
          create_l_entry(footpath{dest, duration_t{0U}});
          // iterate incoming footpaths of target location
          for (auto const fp : tt_.locations_.footpaths_in_[dest]) {
            create_l_entry(fp);
          }
        }
      }
    }
  }

  algo_stats_t get_stats() const { return stats_; }

  void reset_arrivals() {
    state_.r_.reset();
    std::fill(state_.t_min_.begin(), state_.t_min_.end(), unixtime_t::max());
  }

  void next_start_time() { state_.q_.reset(); }

  void add_start(location_idx_t const l, unixtime_t const t) {
    state_.start_location_ = l;
    state_.start_time_ = t;
  }

  void execute(unixtime_t const start_time,
               std::uint8_t const max_transfers,
               unixtime_t const worst_time_at_dest,
               pareto_set<journey>& results);

  void reconstruct(query const& q, journey& j) const;

  struct journey_end {
    journey_end(queue_idx_t const seg_idx,
                l_entry const& le,
                location_idx_t const le_location,
                location_idx_t const last_location)
        : seg_idx_(seg_idx),
          le_(le),
          le_location_(le_location),
          last_location_(last_location) {}

    // the last transport segment of the journey
    queue_idx_t seg_idx_;
    // the l_entry for the destination of the journey
    l_entry le_;
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

  timetable const& tt_;
  tb_query_state& state_;
  std::vector<bool>& is_dest_;
  std::vector<std::uint16_t>& dist_to_dest_;
  std::vector<std::uint16_t>& lb_;
  day_idx_t const base_;
  tb_query_stats stats_;
};

}  // namespace nigiri::routing::tripbased