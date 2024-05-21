#pragma once

#include "nigiri/routing/clasz_mask.h"
#include "nigiri/routing/limits.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

struct reachability_state {
  void resize(unsigned n_locations, unsigned n_routes) {
    location_transfers_.resize(n_locations);
    station_mark_.resize(n_locations);
    prev_station_mark_.resize(n_locations);
    route_mark_.resize(n_routes);
  }

  std::vector<std::uint8_t> location_transfers_;
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec route_mark_;
};

struct reachability_stats {
  std::uint64_t n_footpaths_visited_{0ULL};
  std::uint64_t n_routes_visited_{0ULL};
};

template <direction SearchDir>
struct reachability {
  using algo_state_t = reachability_state;
  using algo_stats_t = reachability_stats;

  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);
  static constexpr auto const kUnreachable =
      std::numeric_limits<std::uint8_t>::max();

  reachability(timetable const& tt,
               reachability_state& state,
               clasz_mask_t const allowed_claszes)
      : tt_{tt},
        state_{state},
        n_locations_{tt_.n_locations()},
        n_routes_{tt.n_routes()},
        allowed_claszes_{allowed_claszes} {
    state_.resize(n_locations_, n_routes_);
  }

  void reset() {
    utl::fill(state_.location_transfers_, kUnreachable);
    utl::fill(state_.prev_station_mark_.blocks_, 0U);
    utl::fill(state_.station_mark_.blocks_, 0U);
    utl::fill(state_.route_mark_.blocks_, 0U);
  }

  void add_dest(location_idx_t const l) {
    state_.station_mark_.set(to_idx(l), true);
  }

  void execute(std::uint8_t const max_transfers, profile_idx_t const prf_idx) {
    auto const end_k = std::min(max_transfers, kMaxTransfers) + 1U;

    for (auto k = 1U; k != end_k; ++k) {

      auto any_marked = false;
      state_.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
        for (auto const& r : tt_.location_routes_[location_idx_t{i}]) {
          any_marked = true;
          state_.route_mark_.set(to_idx(r), true);
        }
      });

      if (!any_marked) {
        break;
      }

      std::swap(state_.prev_station_mark_, state_.station_mark_);
      utl::fill(state_.station_mark_.blocks_, 0U);

      any_marked = (allowed_claszes_ == all_clasz_allowed())
                       ? loop_routes<false>(k)
                       : loop_routes<true>(k);

      if (!any_marked) {
        break;
      }

      utl::fill(state_.route_mark_.blocks_, 0U);

      std::swap(state_.prev_station_mark_, state_.station_mark_);
      utl::fill(state_.station_mark_.blocks_, 0U);

      update_transfers();
      update_footpaths(prf_idx);
    }
  }

private:
  template <bool WithClaszFilter>
  bool loop_routes(unsigned const k) {
    auto any_marked = false;
    state_.route_mark_.for_each_set_bit([&](auto const r_idx) {
      auto const r = route_idx_t{r_idx};

      if constexpr (WithClaszFilter) {
        if (!is_allowed(allowed_claszes_, tt_.route_clasz_[r])) {
          return;
        }
      }

      ++stats_.n_routes_visited_;
      any_marked |= update_route(k, r);
    });
    return any_marked;
  }

  bool update_route(unsigned const k, route_idx_t const r) {
    auto const stop_seq = tt_.route_location_seq_[r];
    bool any_marked = false;

    auto found_reached = false;
    for (auto i = 0U; i != stop_seq.size(); ++i) {
      auto const stop_idx =
          static_cast<stop_idx_t>(kFwd ? i : stop_seq.size() - i - 1U);
      auto const stp = stop{stop_seq[stop_idx]};
      auto const l_idx = cista::to_idx(stp.location_idx());
      auto const is_last = i == stop_seq.size() - 1U;

      if (!found_reached && !state_.prev_station_mark_[l_idx]) {
        continue;
      }

      if (found_reached && (kFwd ? stp.out_allowed() : stp.in_allowed())) {
        state_.station_mark_.set(l_idx, true);
        any_marked = true;
      }

      if (is_last || !(kFwd ? stp.in_allowed() : stp.out_allowed()) ||
          !state_.prev_station_mark_[l_idx]) {
        continue;
      }

      if (state_.location_transfers_[l_idx] == kUnreachable) {
        state_.location_transfers_[l_idx] = k;
      }
    }
    return any_marked;
  }

  void update_transfers() { state_.station_mark_ |= state_.prev_station_mark_; }

  void update_footpaths(profile_idx_t const prf_idx) {
    state_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx][l_idx];
      for (auto const& fp : fps) {
        ++stats_.n_footpaths_visited_;
        state_.station_mark_.set(to_idx(fp.target()), true);
      }
    });
  }

  timetable const& tt_;
  reachability_state& state_;
  reachability_stats stats_;
  std::uint32_t n_locations_, n_routes_;
  clasz_mask_t allowed_claszes_;
};

}  // namespace nigiri::routing