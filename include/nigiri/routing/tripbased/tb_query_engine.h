#pragma once

#include <cinttypes>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/tripbased/tb_preprocessor.h"
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
              for (std::uint16_t stop_idx{0U};
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
              for (std::uint16_t stop_idx{0U};
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

  // reconstructs the journey that ends with the given transport segment
  void reconstruct(query const& q, journey& j);

  // given a journey
  // returns the queue index of the last segment and the relevant l_entry
  // or nullopt if no matching last segment could be found
  std::optional<std::pair<std::uint32_t, l_entry>> find_last_seg(
      journey const& j);

  std::optional<offset> find_MUMO(l_entry const& le,
                                  query const& q,
                                  footpath const& fp);

  static bool is_start(const query& q, const location_idx_t location_idx);

  std::optional<offset> find_closest_start(const query& q,
                                           const location_idx_t location_idx);

  std::optional<location_idx_t> reconstruct_dest(const l_entry& le);

  timetable const& tt_;
  tb_query_state& state_;
  std::vector<bool>& is_dest_;
  std::vector<std::uint16_t>& dist_to_dest_;
  std::vector<std::uint16_t>& lb_;
  day_idx_t const base_;
  tb_query_stats stats_;
};

}  // namespace nigiri::routing::tripbased