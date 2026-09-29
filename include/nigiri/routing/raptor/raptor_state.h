#pragma once

#include <array>
#include <span>
#include <vector>

#include "date/date.h"

#include "cista/containers/bitvec.h"
#include "cista/containers/flat_matrix.h"

#include "nigiri/common/delta_t.h"
#include "nigiri/common/flat_matrix_view.h"
#include "nigiri/routing/limits.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing {

// two best distinct label times at a stop + the rounds they were seen in
struct top2 {
  std::array<delta_t, 2> t_;
  std::array<std::uint8_t, 2> k_;
};

struct raptor_state {
  raptor_state() = default;
  raptor_state(raptor_state const&) = delete;
  raptor_state& operator=(raptor_state const&) = delete;
  raptor_state(raptor_state&&) = default;
  raptor_state& operator=(raptor_state&&) = default;
  ~raptor_state() = default;

  raptor_state& resize(unsigned n_locations,
                       unsigned n_routes,
                       unsigned n_rt_transports);

  // extra slot: second time = time +/- delta, 0 = no second time
  void resize_extra_slot();
  void clear_extra_slot();
  bool has_extra_slot() const { return !round_delta_.empty(); }
  std::uint8_t round_delta(unsigned const k, unsigned const l) const {
    return round_delta_[k * n_locations_ + l];
  }
  std::uint8_t& round_delta(unsigned const k, unsigned const l) {
    return round_delta_[k * n_locations_ + l];
  }

  template <via_offset_t Vias>
  void print(timetable const& tt, date::sys_days, delta_t invalid);

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1>> get_tmp() {
    return {
        reinterpret_cast<std::array<delta_t, Vias + 1>*>(tmp_storage_.data()),
        n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_tmp() const {
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                tmp_storage_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1>> get_best() {
    return {
        reinterpret_cast<std::array<delta_t, Vias + 1>*>(best_storage_.data()),
        n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_best() const {
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                best_storage_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1>> get_round_times() {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                 round_times_storage_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1> const> get_round_times()
      const {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                 round_times_storage_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1>> get_bounds() {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                 bounds_storage_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1> const> get_bounds() const {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                 bounds_storage_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  unsigned n_locations_{};
  std::vector<delta_t> tmp_storage_;
  std::vector<delta_t> best_storage_;
  std::vector<delta_t> round_times_storage_;
  std::vector<delta_t> bounds_storage_;
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec route_mark_;
  bitvec rt_transport_mark_;

  // reference implementation for alternatives: routes that are skipped and
  // locations without entering / exiting, empty = nothing blocked
  bitvec blocked_routes_;
  bitvec blocked_locations_;

  std::vector<std::uint8_t> tmp_delta_;
  std::vector<std::uint8_t> best_delta_;
  std::vector<std::uint8_t> round_delta_;

  // second best statistics
  std::vector<top2> sb_top_;  // reset for each start time
  std::vector<std::uint32_t> sb_second_;  // per stop, never reset
  std::vector<std::uint32_t> sb_evicted_;  // per stop, never reset
};

}  // namespace nigiri::routing
