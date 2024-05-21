#pragma once

#include "nigiri/routing/clasz_mask.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"
#include "search.h"

namespace nigiri::routing {

template <direction SearchDir>
struct reachability {
  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);
  static constexpr auto const kUnreachable =
      std::numeric_limits<std::uint8_t>::max();

  reachability(timetable const& tt,
               raptor_state& rs,
               clasz_mask_t allowed_claszes)
      : tt_{tt}, rs_{rs}, allowed_claszes_{allowed_claszes} {}

  void execute(bitvec const& is_dest,
               std::vector<std::uint8_t>& transports_to_dest,
               std::uint8_t const max_transfers,
               profile_idx_t const prf_idx) {
    rs_.station_mark_.resize(tt_.n_locations());
    utl::fill(rs_.station_mark_.blocks_, 0U);
    rs_.prev_station_mark_.resize((tt_.n_locations()));
    utl::fill(rs_.prev_station_mark_.blocks_, 0U);
    rs_.route_mark_.resize(tt_.n_routes());
    utl::fill(rs_.route_mark_.blocks_, 0U);
    transports_to_dest.resize(tt_.n_locations());
    utl::fill(transports_to_dest, kUnreachable);

    rs_.station_mark_ |= is_dest;
    auto const end_k = std::min(max_transfers, kMaxTransfers);

    for (auto k = std::uint8_t{0U}; k != end_k; ++k) {

      auto any_marked = false;
      rs_.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
        if (transports_to_dest[i] == kUnreachable) {
          transports_to_dest[i] = k;
        }
        for (auto const& r : tt_.location_routes_[location_idx_t{i}]) {
          any_marked = true;
          rs_.route_mark_.set(to_idx(r), true);
        }
      });

      if (!any_marked) {
        break;
      }

      std::swap(rs_.prev_station_mark_, rs_.station_mark_);
      utl::fill(rs_.station_mark_.blocks_, 0U);

      any_marked = (allowed_claszes_ == all_clasz_allowed())
                       ? loop_routes<false>()
                       : loop_routes<true>();

      if (!any_marked) {
        break;
      }

      utl::fill(rs_.route_mark_.blocks_, 0U);

      std::swap(rs_.prev_station_mark_, rs_.station_mark_);
      utl::fill(rs_.station_mark_.blocks_, 0U);

      update_transfers();
      update_footpaths(prf_idx);
    }
  }

private:
  template <bool WithClaszFilter>
  bool loop_routes() {
    auto any_marked = false;
    rs_.route_mark_.for_each_set_bit([&](auto const r_idx) {
      auto const r = route_idx_t{r_idx};

      if constexpr (WithClaszFilter) {
        if (!is_allowed(allowed_claszes_, tt_.route_clasz_[r])) {
          return;
        }
      }

      any_marked |= update_route(r);
    });
    return any_marked;
  }

  bool update_route(route_idx_t const r) {
    auto const stop_seq = tt_.route_location_seq_[r];
    bool any_marked = false;

    auto found_reached = false;
    for (auto i = 0U; i != stop_seq.size(); ++i) {
      auto const stop_idx =
          static_cast<stop_idx_t>(kFwd ? i : stop_seq.size() - i - 1U);
      auto const stp = stop{stop_seq[stop_idx]};
      auto const l_idx = cista::to_idx(stp.location_idx());
      auto const is_last = i == stop_seq.size() - 1U;

      if (!found_reached && !rs_.prev_station_mark_[l_idx]) {
        continue;
      }

      if (found_reached && (kFwd ? stp.out_allowed() : stp.in_allowed())) {
        rs_.station_mark_.set(l_idx, true);
        any_marked = true;
      }

      if (is_last || !(kFwd ? stp.in_allowed() : stp.out_allowed()) ||
          !rs_.prev_station_mark_[l_idx]) {
        continue;
      }

      found_reached = true;
    }
    return any_marked;
  }

  void update_transfers() { rs_.station_mark_ |= rs_.prev_station_mark_; }

  void update_footpaths(profile_idx_t const prf_idx) {
    rs_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx][l_idx];
      for (auto const& fp : fps) {
        rs_.station_mark_.set(to_idx(fp.target()), true);
      }
    });
  }

  timetable const& tt_;
  raptor_state& rs_;
  clasz_mask_t allowed_claszes_;
};

}  // namespace nigiri::routing