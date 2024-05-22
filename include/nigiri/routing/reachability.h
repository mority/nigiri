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
  static constexpr auto const kUnreached =
      std::numeric_limits<std::uint8_t>::max();

  reachability(timetable const& tt,
               raptor_state& rs,
               std::vector<std::uint8_t>& transports_to_dest,
               clasz_mask_t allowed_claszes)
      : tt_{tt},
        rs_{rs},
        transports_to_dest_{transports_to_dest},
        allowed_claszes_{allowed_claszes} {}

  void execute(bitvec const& is_dest,
               std::uint8_t const max_transfers,
               profile_idx_t const prf_idx) {
    // prepare state
    rs_.station_mark_.resize(tt_.n_locations());
    rs_.station_mark_ = is_dest;
    rs_.prev_station_mark_.resize((tt_.n_locations()));
    utl::fill(rs_.prev_station_mark_.blocks_, 0U);
    rs_.route_mark_.resize(tt_.n_routes());
    utl::fill(rs_.route_mark_.blocks_, 0U);
    transports_to_dest_.resize(tt_.n_locations());
    utl::fill(transports_to_dest_, kUnreached);

    // init
    rs_.station_mark_.for_each_set_bit(
        [&](std::uint64_t const i) { transports_to_dest_[i] = 0; });
    end_k_ = std::min(max_transfers, kMaxTransfers) + 1U;

    // rounds
    for (auto k = std::uint8_t{1U}; k != end_k_; ++k) {
      auto any_marked = false;
      rs_.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
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
                       ? loop_routes<false>(k)
                       : loop_routes<true>(k);

      if (!any_marked) {
        break;
      }

      utl::fill(rs_.route_mark_.blocks_, 0U);

      std::swap(rs_.prev_station_mark_, rs_.station_mark_);

      rs_.station_mark_ = rs_.prev_station_mark_;  // update_transfers();
      update_footpaths(k, prf_idx);
    }
  }

private:
  template <bool WithClaszFilter>
  bool loop_routes(std::uint8_t const k) {
    auto any_marked = false;
    rs_.route_mark_.for_each_set_bit([&](auto const r_idx) {
      auto const r = route_idx_t{r_idx};

      if constexpr (WithClaszFilter) {
        if (!is_allowed(allowed_claszes_, tt_.route_clasz_[r])) {
          return;
        }
      }

      any_marked |= update_route(r, k);
    });
    return any_marked;
  }

  bool update_route(route_idx_t const r, std::uint8_t const k) {
    auto const stop_seq = tt_.route_location_seq_[r];
    bool any_marked = false;

    auto found_prev_mark = false;
    for (auto i = 0U; i != stop_seq.size(); ++i) {
      auto const stop_idx =
          static_cast<stop_idx_t>(kFwd ? i : stop_seq.size() - i - 1U);
      auto const stp = stop{stop_seq[stop_idx]};
      auto const l_idx = cista::to_idx(stp.location_idx());
      auto const is_last = i == stop_seq.size() - 1U;

      if (!found_prev_mark && !rs_.prev_station_mark_[l_idx]) {
        continue;
      }

      if (found_prev_mark && (kFwd ? stp.out_allowed() : stp.in_allowed()) &&
          transports_to_dest_[l_idx] == kUnreached) {
        transports_to_dest_[l_idx] = k;
        rs_.station_mark_.set(l_idx, true);
        any_marked = true;
      }

      if (is_last || !(kFwd ? stp.in_allowed() : stp.out_allowed()) ||
          !rs_.prev_station_mark_[l_idx]) {
        continue;
      }

      found_prev_mark = true;
    }
    return any_marked;
  }

  void update_footpaths(std::uint8_t const k, profile_idx_t const prf_idx) {
    rs_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx][l_idx];
      for (auto const& fp : fps) {
        auto const l_idx_fp = to_idx(fp.target());
        if (transports_to_dest_[l_idx_fp] == kUnreached) {
          transports_to_dest_[l_idx_fp] = k;
          rs_.station_mark_.set(l_idx_fp, true);
        }
      }
    });
  }

  timetable const& tt_;
  raptor_state& rs_;
  std::vector<std::uint8_t>& transports_to_dest_;
  clasz_mask_t allowed_claszes_;
  std::uint8_t end_k_;
};

}  // namespace nigiri::routing