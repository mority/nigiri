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

struct raptor_state {
  raptor_state() = default;
  raptor_state(raptor_state const&) = delete;
  raptor_state& operator=(raptor_state const&) = delete;
  raptor_state(raptor_state&&) = default;
  raptor_state& operator=(raptor_state&&) = default;
  ~raptor_state() = default;

  raptor_state& resize(unsigned n_locations,
                       unsigned n_routes,
                       unsigned n_rt_transports,
                       bool with_sched);

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
  std::span<std::array<delta_t, Vias + 1>> get_tmp_sched() {
    return {reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                tmp_storage_sched_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_tmp_sched() const {
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                tmp_storage_sched_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1>> get_best_sched() {
    return {reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                best_storage_sched_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_best_sched() const {
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                best_storage_sched_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1>> get_round_times_sched() {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                 round_times_storage_sched_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1> const> get_round_times_sched()
      const {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                 round_times_storage_sched_.data()),
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

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1>> get_bounds_sched() {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                 bounds_storage_sched_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1> const> get_bounds_sched()
      const {
    return {{reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                 bounds_storage_sched_.data()),
             n_locations_ * (kMaxTransfers + 2)},
            kMaxTransfers + 2U,
            n_locations_};
  }

  unsigned n_locations_{};
  std::vector<delta_t> tmp_storage_;
  std::vector<delta_t> best_storage_;
  std::vector<delta_t> round_times_storage_;
  std::vector<delta_t> bounds_storage_;
  // The rt slot's marks, like tmp_/best_/round_times_ above: a location or
  // route is marked here because the *rt* slot improved there. For
  // rt_mode::off/on that is the only slot, so these are simply "the" marks.
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec route_mark_;
  bitvec rt_transport_mark_;

  // The scheduled slot's marks for rt_mode::both -- never a union with the
  // rt ones above. A slot can only ever board where that same slot has a
  // label from the previous round, so mixing the two just makes each slot
  // re-walk stop sequences the other one reached. Where both sets agree the
  // route is still scanned only once, with both slots active.
  // Empty (and unused) for rt_mode::off/on.
  bitvec station_mark_sched_;
  bitvec prev_station_mark_sched_;
  bitvec route_mark_sched_;

  // Scheduled-slot state for rt_mode::both (see get_*_sched() above).
  // Unused (but still allocated) by rt_mode::off/on.
  std::vector<delta_t> tmp_storage_sched_;
  std::vector<delta_t> best_storage_sched_;
  std::vector<delta_t> round_times_storage_sched_;
  std::vector<delta_t> bounds_storage_sched_;
};

}  // namespace nigiri::routing
