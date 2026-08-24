#pragma once

#include <cassert>
#include <algorithm>
#include <span>
#include <type_traits>
#include <utility>

#include "nigiri/common/delta_t.h"
#include "nigiri/common/linear_lower_bound.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/raptor/debug.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor/raptor_stats.h"
#include "nigiri/routing/raptor/reconstruct.h"
#include "nigiri/routing/raptor/rt_mode.h"
#include "nigiri/routing/transfer_time_settings.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

enum class search_mode { kOneToOne, kOneToAll };

template <direction SearchDir,
          rt_mode RtMode,
          via_offset_t Vias,
          search_mode SearchMode>
struct raptor {
  using algo_state_t = raptor_state;
  using algo_stats_t = raptor_stats;

  static constexpr auto kRtMode = RtMode;
  static constexpr bool kUseLowerBounds = true;
  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);
  static constexpr auto const kInvalid = kInvalidDelta<SearchDir>;
  static constexpr auto const kUnreachable =
      std::numeric_limits<std::uint16_t>::max();
  static constexpr auto const kIntermodalTarget =
      to_idx(get_special_station(special_station::kEnd));
  static constexpr auto const kInvalidArray = []() {
    auto a = std::array<delta_t, Vias + 1>{};
    a.fill(kInvalid);
    return a;
  }();

  static bool is_better(auto a, auto b) { return kFwd ? a < b : a > b; }
  static bool is_better_or_eq(auto a, auto b) { return kFwd ? a <= b : a >= b; }
  static auto get_best(auto a, auto b) { return is_better(a, b) ? a : b; }
  static auto get_best(auto x, auto... y) {
    ((x = get_best(x, y)), ...);
    return x;
  }
  static auto dir(auto a) { return (kFwd ? 1 : -1) * a; }

  raptor(
      timetable const& tt,
      rt_timetable const* rtt,
      raptor_state& state,
      bitvec& is_dest,
      std::array<bitvec, kMaxVias>& is_via,
      std::vector<std::uint16_t>& dist_to_dest,
      hash_map<location_idx_t, std::vector<td_offset>> const& td_dist_to_dest,
      std::vector<std::uint16_t>& lb,
      std::vector<via_stop> const& via_stops,
      day_idx_t const base,
      clasz_mask_t const allowed_claszes,
      bool const require_bike_transport,
      bool const require_car_transport,
      bool const is_wheelchair,
      bool const no_compulsory_reservation,
      transfer_time_settings const& tts,
      profile_idx_t const prf_idx)
      : tt_{tt},
        rtt_{rtt},
        n_days_{tt_.internal_interval_days().size().count()},
        n_locations_{tt_.n_locations()},
        n_routes_{tt.n_routes()},
        n_rt_transports_{RtMode != rt_mode::off ? rtt->n_rt_transports() : 0U},
        state_{state.resize(n_locations_, n_routes_, n_rt_transports_,
                            RtMode == rt_mode::both)},
        tmp_{state_.get_tmp<Vias>()},
        best_{state_.get_best<Vias>()},
        round_times_{state.get_round_times<Vias>()},
        tmp_sched_{state_.get_tmp_sched<Vias>()},
        best_sched_{state_.get_best_sched<Vias>()},
        round_times_sched_{state.get_round_times_sched<Vias>()},
        is_dest_{is_dest},
        is_via_{is_via},
        dist_to_end_{dist_to_dest},
        td_dist_to_end_{td_dist_to_dest},
        lb_{lb},
        via_stops_{via_stops},
        base_{base},
        bounds_{std::as_const(state_).template get_bounds<Vias>()},
        prf_idx_{prf_idx},
        allowed_claszes_{allowed_claszes},
        require_bike_transport_{require_bike_transport},
        require_car_transport_{require_car_transport},
        no_compulsory_reservation_{no_compulsory_reservation},
        is_wheelchair_{is_wheelchair},
        transfer_time_settings_{tts} {
    assert(Vias == via_stops_.size());
    reset_arrivals();
    if (!dist_to_end_.empty()) {
      // only used for intermodal queries (dist_to_dest != empty)
      end_reachable_.resize(n_locations_);
      for (auto i = 0U; i != dist_to_end_.size(); ++i) {
        if (dist_to_end_[i] != kUnreachable) {
          end_reachable_.set(i, true);
        }
      }
      for (auto const& [l, _] : td_dist_to_end_) {
        end_reachable_.set(to_idx(l), true);
      }
    }
  }

  algo_stats_t get_stats() const { return stats_; }

  void fill_bounds(std::size_t const n_rows, bool const for_sched = false) {
    auto& s = state_;
    auto const n = static_cast<std::size_t>(s.n_locations_);

    auto const td_stops = !for_sched && rtt_ != nullptr && prf_idx_ != 0U
                              ? &(kFwd ? rtt_->has_td_footpaths_out_
                                       : rtt_->has_td_footpaths_in_)[prf_idx_]
                              : nullptr;

    auto const src = !for_sched ? std::as_const(s).template get_round_times<Vias>() : std::as_const(s).template get_round_times_sched<Vias>();
    auto dst = !for_sched ? s.template get_bounds<Vias>() : s.template get_bounds_sched<Vias>();

    // Copy k=0 verbatim (rest is folded from here).
    for (auto x = std::size_t{0U}; x != n; ++x) {
      dst[0U][x] = src[0U][x];
    }

    // Fill gaps from lower rounds to higher rounds.
    for (auto k = std::size_t{1U}; k < n_rows; ++k) {
      for (auto x = std::size_t{0U}; x != n; ++x) {
        for (auto v = std::size_t{0U}; v != Vias + 1U; ++v) {
          dst[k][x][v] = kFwd ? std::min(src[k][x][v], dst[k - 1U][x][v])
                              : std::max(src[k][x][v], dst[k - 1U][x][v]);
        }
      }
    }

    if constexpr (Vias != 0U) {
      // Fill gaps from higher vias to lower vias.
      for (auto k = std::size_t{0U}; k != n_rows; ++k) {
        for (auto x = std::size_t{0U}; x != n; ++x) {
          auto& slots = dst[k][x];
          for (auto v = std::size_t{Vias}; v != 0U; --v) {
            slots[v - 1U] = kFwd ? std::min(slots[v - 1U], slots[v])
                                 : std::max(slots[v - 1U], slots[v]);
          }
        }
      }
    }

    if (td_stops != nullptr) {
      // td_footpaths have no upper bound -> disable pruning
      constexpr auto const kPassAll = kFwd
                                          ? std::numeric_limits<delta_t>::min()
                                          : std::numeric_limits<delta_t>::max();
      td_stops->for_each_set_bit([&](location_idx_t const x) {
        for (auto k = std::size_t{0U}; k != n_rows; ++k) {
          for (auto v = std::size_t{0U}; v != Vias + 1U; ++v) {
            dst[k][to_idx(x)][v] = kPassAll;
          }
        }
      });
    }
  }

  void set_bounds(unsigned const last_round) { bounds_last_k_ = last_round; }

  // Records an improvement at location l for one slot, in that slot's own
  // mark set -- the two are never merged, see raptor_state.h.
  template <bool ForSched>
  void mark_station(std::size_t const l) {
    if constexpr (RtMode == rt_mode::both && ForSched) {
      state_.station_mark_sched_.set(l, true);
    } else {
      state_.station_mark_.set(l, true);
    }
  }

  void clear_station_marks() {
    utl::fill(state_.station_mark_.blocks_, 0U);
    if constexpr (RtMode == rt_mode::both) {
      utl::fill(state_.station_mark_sched_.blocks_, 0U);
    }
  }

  void swap_station_marks() {
    std::swap(state_.prev_station_mark_, state_.station_mark_);
    if constexpr (RtMode == rt_mode::both) {
      std::swap(state_.prev_station_mark_sched_, state_.station_mark_sched_);
    }
    clear_station_marks();
  }

  void clear_route_marks() {
    utl::fill(state_.route_mark_.blocks_, 0U);
    if constexpr (RtMode == rt_mode::both) {
      utl::fill(state_.route_mark_sched_.blocks_, 0U);
    }
  }

  void reset_arrivals() {
    utl::fill(time_at_dest_, kInvalid);
    round_times_.reset(kInvalidArray);
    if constexpr (RtMode == rt_mode::both) {
      utl::fill(time_at_dest_sched_, kInvalid);
      round_times_sched_.reset(kInvalidArray);
    }
  }

  void next_start_time() {
    utl::fill(best_, kInvalidArray);
    utl::fill(tmp_, kInvalidArray);
    utl::fill(state_.prev_station_mark_.blocks_, 0U);
    clear_station_marks();
    clear_route_marks();
    if constexpr (RtMode != rt_mode::off) {
      utl::fill(state_.rt_transport_mark_.blocks_, 0U);
    }
    if constexpr (RtMode == rt_mode::both) {
      utl::fill(state_.prev_station_mark_sched_.blocks_, 0U);
      utl::fill(best_sched_, kInvalidArray);
      utl::fill(tmp_sched_, kInvalidArray);
    }
  }

  void add_start(location_idx_t const l, unixtime_t const t) {
    auto const v = (Vias != 0 && is_via_[0][to_idx(l)]) ? 1U : 0U;
    trace_upd(
        "adding start [fwd={}] {}: {}, v={} [current: best={}, round={} => "
        "best={}]\n",
        kFwd, loc{tt_, l}, t, v, to_unix(best_[to_idx(l)][v]),
        to_unix(round_times_[0U][to_idx(l)][v]),
        get_best(t, to_unix(best_[to_idx(l)][v])));
    best_[to_idx(l)][v] =
        get_best(unix_to_delta(base(), t), best_[to_idx(l)][v]);
    round_times_[0U][to_idx(l)][v] =
        get_best(unix_to_delta(base(), t), round_times_[0U][to_idx(l)][v]);
    mark_station<false>(to_idx(l));
    if constexpr (RtMode == rt_mode::both) {
      best_sched_[to_idx(l)][v] =
          get_best(unix_to_delta(base(), t), best_sched_[to_idx(l)][v]);
      round_times_sched_[0U][to_idx(l)][v] = get_best(
          unix_to_delta(base(), t), round_times_sched_[0U][to_idx(l)][v]);
      mark_station<true>(to_idx(l));
    }
  }

  void execute(unixtime_t const start_time,
               std::uint8_t const max_transfers,
               unixtime_t const worst_time_at_dest,
               pareto_set<journey>& results,
               pareto_set<journey>* results_sched = nullptr) {
    auto const end_k = std::min(max_transfers, kMaxTransfers) + 2U;

    auto const d_worst_at_dest = unix_to_delta(base(), worst_time_at_dest);
    for (auto& time_at_dest : time_at_dest_) {
      time_at_dest = get_best(d_worst_at_dest, time_at_dest);
    }
    if constexpr (RtMode == rt_mode::both) {
      for (auto& time_at_dest : time_at_dest_sched_) {
        time_at_dest = get_best(d_worst_at_dest, time_at_dest);
      }
    }

    trace_print_init_state();

    for (auto k = 1U; k != end_k; ++k) {
      for (auto i = 0U; i != n_locations_; ++i) {
        for (auto v = 0U; v != Vias + 1; ++v) {
          best_[i][v] = get_best(round_times_[k][i][v], best_[i][v]);
          if constexpr (RtMode == rt_mode::both) {
            best_sched_[i][v] =
                get_best(round_times_sched_[k][i][v], best_sched_[i][v]);
          }
        }
      }
      is_dest_.for_each_set_bit([&](std::uint64_t const i) {
        update_time_at_dest(k, best_[i][Vias]);
        if constexpr (RtMode == rt_mode::both) {
          update_time_at_dest<true>(k, best_sched_[i][Vias]);
        }
      });

      // Each slot expands its own marks: a slot can only board where that
      // same slot has a round-(k-1) label, and round_times_[k-1] is set
      // exactly where that slot marked. (Labels carried over from an earlier
      // start time of a range search are the one exception -- round_times_
      // set without a mark -- and they need no re-expansion: next_start_time()
      // clears every mark while keeping round_times_, so the search already
      // relies on that invariant. Splitting the marks only applies it per
      // slot.) Routes reached by both slots land in both route mark sets and
      // are scanned once for both, see for_each_marked_route().
      auto any_marked = false;
      state_.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
        for (auto const& r : tt_.location_routes_[location_idx_t{i}]) {
          any_marked = true;
          state_.route_mark_.set(to_idx(r), true);
        }
        // rt transports are an rtt_-only concept: only the rt slot rides them.
        if constexpr (RtMode != rt_mode::off) {
          for (auto const& rt_t :
               rtt_->location_rt_transports_[location_idx_t{i}]) {
            any_marked = true;
            state_.rt_transport_mark_.set(to_idx(rt_t), true);
          }
        }
      });
      if constexpr (RtMode == rt_mode::both) {
        state_.station_mark_sched_.for_each_set_bit([&](std::uint64_t const i) {
          for (auto const& r : tt_.location_routes_[location_idx_t{i}]) {
            any_marked = true;
            state_.route_mark_sched_.set(to_idx(r), true);
          }
        });
      }
      if (!any_marked) {
        trace_print_state_after_round();
        break;
      }

      swap_station_marks();

      bool const clasz_filter = allowed_claszes_ != all_clasz_allowed();
      uint8_t const filters =
          static_cast<uint8_t>(clasz_filter << 4) |
          static_cast<uint8_t>(require_bike_transport_ << 3) |
          static_cast<uint8_t>(require_car_transport_ << 2) |
          static_cast<uint8_t>(is_wheelchair_ << 1) |
          static_cast<uint8_t>(no_compulsory_reservation_ << 0);

      any_marked |= [&]() {
        switch (filters) {
          case 0b00000:
            return loop_routes<false, false, false, false, false>(k);
          case 0b00001: return loop_routes<false, false, false, false, true>(k);
          case 0b00010: return loop_routes<false, false, false, true, false>(k);
          case 0b00011: return loop_routes<false, false, false, true, true>(k);
          case 0b00100: return loop_routes<false, false, true, false, false>(k);
          case 0b00101: return loop_routes<false, false, true, false, true>(k);
          case 0b00110: return loop_routes<false, false, true, true, false>(k);
          case 0b00111: return loop_routes<false, false, true, true, true>(k);
          case 0b01000: return loop_routes<false, true, false, false, false>(k);
          case 0b01001: return loop_routes<false, true, false, false, true>(k);
          case 0b01010: return loop_routes<false, true, false, true, false>(k);
          case 0b01011: return loop_routes<false, true, false, true, true>(k);
          case 0b01100: return loop_routes<false, true, true, false, false>(k);
          case 0b01101: return loop_routes<false, true, true, false, true>(k);
          case 0b01110: return loop_routes<false, true, true, true, false>(k);
          case 0b01111: return loop_routes<false, true, true, true, true>(k);
          case 0b10000: return loop_routes<true, false, false, false, false>(k);
          case 0b10001: return loop_routes<true, false, false, false, true>(k);
          case 0b10010: return loop_routes<true, false, false, true, false>(k);
          case 0b10011: return loop_routes<true, false, false, true, true>(k);
          case 0b10100: return loop_routes<true, false, true, false, false>(k);
          case 0b10101: return loop_routes<true, false, true, false, true>(k);
          case 0b10110: return loop_routes<true, false, true, true, false>(k);
          case 0b10111: return loop_routes<true, false, true, true, true>(k);
          case 0b11000: return loop_routes<true, true, false, false, false>(k);
          case 0b11001: return loop_routes<true, true, false, false, true>(k);
          case 0b11010: return loop_routes<true, true, false, true, false>(k);
          case 0b11011: return loop_routes<true, true, false, true, true>(k);
          case 0b11100: return loop_routes<true, true, true, false, false>(k);
          case 0b11101: return loop_routes<true, true, true, false, true>(k);
          case 0b11110: return loop_routes<true, true, true, true, false>(k);
          case 0b11111: return loop_routes<true, true, true, true, true>(k);
          default: std::unreachable();
        }
      }();

      if constexpr (RtMode != rt_mode::off) {
        any_marked |= [&]() {
          switch (filters) {
            case 0b00000:
              return loop_rt_routes<false, false, false, false, false>(k);
            case 0b00001:
              return loop_rt_routes<false, false, false, false, true>(k);
            case 0b00010:
              return loop_rt_routes<false, false, false, true, false>(k);
            case 0b00011:
              return loop_rt_routes<false, false, false, true, true>(k);
            case 0b00100:
              return loop_rt_routes<false, false, true, false, false>(k);
            case 0b00101:
              return loop_rt_routes<false, false, true, false, true>(k);
            case 0b00110:
              return loop_rt_routes<false, false, true, true, false>(k);
            case 0b00111:
              return loop_rt_routes<false, false, true, true, true>(k);
            case 0b01000:
              return loop_rt_routes<false, true, false, false, false>(k);
            case 0b01001:
              return loop_rt_routes<false, true, false, false, true>(k);
            case 0b01010:
              return loop_rt_routes<false, true, false, true, false>(k);
            case 0b01011:
              return loop_rt_routes<false, true, false, true, true>(k);
            case 0b01100:
              return loop_rt_routes<false, true, true, false, false>(k);
            case 0b01101:
              return loop_rt_routes<false, true, true, false, true>(k);
            case 0b01110:
              return loop_rt_routes<false, true, true, true, false>(k);
            case 0b01111:
              return loop_rt_routes<false, true, true, true, true>(k);
            case 0b10000:
              return loop_rt_routes<true, false, false, false, false>(k);
            case 0b10001:
              return loop_rt_routes<true, false, false, false, true>(k);
            case 0b10010:
              return loop_rt_routes<true, false, false, true, false>(k);
            case 0b10011:
              return loop_rt_routes<true, false, false, true, true>(k);
            case 0b10100:
              return loop_rt_routes<true, false, true, false, false>(k);
            case 0b10101:
              return loop_rt_routes<true, false, true, false, true>(k);
            case 0b10110:
              return loop_rt_routes<true, false, true, true, false>(k);
            case 0b10111:
              return loop_rt_routes<true, false, true, true, true>(k);
            case 0b11000:
              return loop_rt_routes<true, true, false, false, false>(k);
            case 0b11001:
              return loop_rt_routes<true, true, false, false, true>(k);
            case 0b11010:
              return loop_rt_routes<true, true, false, true, false>(k);
            case 0b11011:
              return loop_rt_routes<true, true, false, true, true>(k);
            case 0b11100:
              return loop_rt_routes<true, true, true, false, false>(k);
            case 0b11101:
              return loop_rt_routes<true, true, true, false, true>(k);
            case 0b11110:
              return loop_rt_routes<true, true, true, true, false>(k);
            case 0b11111:
              return loop_rt_routes<true, true, true, true, true>(k);
            default: std::unreachable();
          }
        }();
      }

      if (!any_marked) {
        trace_print_state_after_round();
        break;
      }

      clear_route_marks();
      utl::fill(state_.rt_transport_mark_.blocks_, 0U);

      swap_station_marks();

      update_transfers(k);
      update_intermodal_footpaths(k);
      update_footpaths(k);
      update_td_offsets(k);

      trace_print_state_after_round();
    }

    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }

    is_dest_.for_each_set_bit([&](auto const i) {
      for (auto k = 1U; k != end_k; ++k) {
        auto const dest_time = round_times_[k][i][Vias];
        if (dest_time != kInvalid) {
          trace("ADDING JOURNEY: start={}, dest={} @ {}, transfers={}\n",
                start_time, delta_to_unix(base(), round_times_[k][i][Vias]),
                loc{tt_, location_idx_t{i}}, k - 1);
          auto const [optimal, it, dominated_by] = results.add(
              journey{.legs_ = {},
                      .start_time_ = start_time,
                      .dest_time_ = delta_to_unix(base(), dest_time),
                      .dest_ = location_idx_t{i},
                      .transfers_ = static_cast<std::uint8_t>(k - 1)});
          if (!optimal) {
            trace("  DOMINATED BY: start={}, dest={} @ {}, transfers={}\n",
                  dominated_by->start_time_, dominated_by->dest_time_,
                  loc{tt_, dominated_by->dest_}, dominated_by->transfers_);
          }
        }
      }
    });

    if constexpr (RtMode == rt_mode::both) {
      if (results_sched != nullptr) {
        is_dest_.for_each_set_bit([&](auto const i) {
          for (auto k = 1U; k != end_k; ++k) {
            auto const dest_time = round_times_sched_[k][i][Vias];
            if (dest_time != kInvalid) {
              results_sched->add(
                  journey{.legs_ = {},
                          .start_time_ = start_time,
                          .dest_time_ = delta_to_unix(base(), dest_time),
                          .dest_ = location_idx_t{i},
                          .transfers_ = static_cast<std::uint8_t>(k - 1)});
            }
          }
        });
      }
    }
  }

  void reconstruct(query const& q, journey& j, bool const for_sched = false) {
    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }
    trace("reconstruct({} - {}, {} transfers", j.departure_time(),
          j.arrival_time(), j.transfers_);
    if (for_sched) {
      reconstruct_journey<SearchDir,true>(tt_, nullptr, q, state_, j, base(), base_);
    } else {
      reconstruct_journey<SearchDir,false>(tt_, rtt_, q, state_, j, base(), base_);
    }
  }

private:
  date::sys_days base() const {
    return tt_.internal_interval_days().from_ + as_int(base_) * date::days{1};
  }

  std::uint16_t get_lb(std::uint32_t const i) const {
    if constexpr (kUseLowerBounds) {
      assert(i < lb_.size());
      return lb_[i];
    } else {
      return 0U;
    }
  }

  bool lb_reachable(std::uint32_t const i) const {
    if constexpr (kUseLowerBounds) {
      assert(i < lb_.size());
      return lb_[i] != kUnreachable;
    } else {
      return true;
    }
  }

  // For ping: comparison has to be <= instead of < so cells with equal arrival
  // times are still populated. Otherwise, the ping bounds are too strict for
  // pong to still find all optimal journeys.
  //
  // Loose pruning with <= instead of < is always enabled on the CPU.
  // Measured to have ~ the same performance as strict pruning.
  bool is_better_loose(auto const a, auto const b) const {
    return is_better_or_eq(a, b);
  }

  bool within_bounds(unsigned const k,
                     std::size_t const l,
                     delta_t const t,
                     std::size_t const v) const {
    if (bounds_last_k_ == 0U) {
      return true;
    }

    assert(k <= bounds_last_k_);
    assert(v <= Vias);

    auto const row = bounds_[bounds_last_k_ - k];
    auto const slot = Vias - v;
    auto const via_stays = [&](std::size_t const stop) {
      auto stays = 0;
      if constexpr (Vias != 0U) {
        for (auto w = std::size_t{0U}; w != Vias; ++w) {
          if (is_via_[w][static_cast<bitvec::size_type>(stop)]) {
            stays += static_cast<int>(via_stops_[w].stay_.count());
          }
        }
      }
      return stays;
    };

    auto const stays_l = via_stays(l);
    auto const transfer = dir(adjusted_transfer_time(
        transfer_time_settings_,
        static_cast<int>(
            tt_.locations_.transfer_time_[location_idx_t{l}].count())));
    return is_better_or_eq(t, row[l][slot] + transfer + dir(stays_l));
  }

  // Visits every route either slot marked, exactly once, and tells `fn` which
  // slots want it: fn(r, do_rt, do_sched). A route only one slot reached is
  // scanned for that slot alone -- the other has no round-(k-1) label at any
  // of its stops and could not board it anyway. A route both slots reached is
  // scanned once with both active: the stop sequence walk, the section
  // filters and the via bookkeeping are schedule-independent, so only the
  // per-slot label updates have to happen twice.
  template <typename Fn>
  void for_each_marked_route(Fn&& fn) {
    state_.route_mark_.for_each_set_bit([&](auto const r_idx) {
      if constexpr (RtMode == rt_mode::both) {
        fn(route_idx_t{r_idx}, true,
           state_.route_mark_sched_.test(
               static_cast<bitvec::size_type>(r_idx)));
      } else {
        fn(route_idx_t{r_idx}, true, false);
      }
    });
    if constexpr (RtMode == rt_mode::both) {
      state_.route_mark_sched_.for_each_set_bit([&](auto const r_idx) {
        // the shared ones already ran above, with both slots active
        if (!state_.route_mark_.test(static_cast<bitvec::size_type>(r_idx))) {
          fn(route_idx_t{r_idx}, false, true);
        }
      });
    }
  }

  // The same walk over the locations each slot improved in the previous
  // round, for the footpath/transfer phase: fn(l, do_rt, do_sched). A
  // location both slots reached is visited once, so the per-location work
  // that does not depend on the schedule -- via state, transfer time, the
  // footpath list -- is done once for both.
  template <typename Fn>
  void for_each_prev_marked(Fn&& fn) {
    state_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      if constexpr (RtMode == rt_mode::both) {
        fn(i, true,
           state_.prev_station_mark_sched_.test(
               static_cast<bitvec::size_type>(i)));
      } else {
        fn(i, true, false);
      }
    });
    if constexpr (RtMode == rt_mode::both) {
      state_.prev_station_mark_sched_.for_each_set_bit(
          [&](std::uint64_t const i) {
            if (!state_.prev_station_mark_.test(
                    static_cast<bitvec::size_type>(i))) {
              fn(i, false, true);
            }
          });
    }
  }

  template <bool WithClaszFilter,
            bool WithBikeFilter,
            bool WithCarFilter,
            bool WithWheelchairFilter,
            bool WithReservationNotRequiredFilter>
  bool loop_routes(unsigned const k) {
    auto any_marked = false;
    for_each_marked_route([&](route_idx_t const r, bool const do_rt,
                              bool const do_sched) {
      auto const r_idx = static_cast<std::size_t>(to_idx(r));

      if constexpr (WithClaszFilter) {
        if (!is_allowed(allowed_claszes_, tt_.route_clasz_[r])) {
          return;
        }
      }

      auto filters = static_cast<uint8_t>(0);

      auto const apply_filter = [&](route_flag const f) {
        auto const flag_set_on_all_sections =
            tt_.route_flags_[f].test(r_idx * 2);
        if (!flag_set_on_all_sections) {
          auto const flag_set_on_some_sections =
              tt_.route_flags_[f].test(r_idx * 2 + 1);
          if (!flag_set_on_some_sections) {
            return false;
          }
          filters |=
              static_cast<uint8_t>(1 << (route_flag::kNumRouteFlags - f - 1));
          return true;
        }
        return true;
      };

      if constexpr (WithBikeFilter) {
        if (!apply_filter(route_flag::kBikesAllowed)) {
          return;
        }
      }

      if constexpr (WithCarFilter) {
        if (!apply_filter(route_flag::kCarsAllowed)) {
          return;
        }
      }

      if constexpr (WithWheelchairFilter) {
        if (!apply_filter(route_flag::kWheelchairAccessible)) {
          return;
        }
      }

      if constexpr (WithReservationNotRequiredFilter) {
        if (!apply_filter(route_flag::kReservationNotRequired)) {
          return;
        }
      }

      ++stats_.n_routes_visited_;
      trace("┊ ├k={} updating route {}\n", k, r);

      any_marked |= [&]() {
        switch (filters) {
          case 0b0000:
            return update_route<false, false, false, false>(k, r, do_rt,
                                                            do_sched);
          case 0b0001:
            return update_route<false, false, false, true>(k, r, do_rt,
                                                           do_sched);
          case 0b0010:
            return update_route<false, false, true, false>(k, r, do_rt,
                                                           do_sched);
          case 0b0011:
            return update_route<false, false, true, true>(k, r, do_rt,
                                                          do_sched);
          case 0b0100:
            return update_route<false, true, false, false>(k, r, do_rt,
                                                           do_sched);
          case 0b0101:
            return update_route<false, true, false, true>(k, r, do_rt,
                                                          do_sched);
          case 0b0110:
            return update_route<false, true, true, false>(k, r, do_rt,
                                                          do_sched);
          case 0b0111:
            return update_route<false, true, true, true>(k, r, do_rt,
                                                         do_sched);
          case 0b1000:
            return update_route<true, false, false, false>(k, r, do_rt,
                                                           do_sched);
          case 0b1001:
            return update_route<true, false, false, true>(k, r, do_rt,
                                                          do_sched);
          case 0b1010:
            return update_route<true, false, true, false>(k, r, do_rt,
                                                          do_sched);
          case 0b1011:
            return update_route<true, false, true, true>(k, r, do_rt,
                                                         do_sched);
          case 0b1100:
            return update_route<true, true, false, false>(k, r, do_rt,
                                                          do_sched);
          case 0b1101:
            return update_route<true, true, false, true>(k, r, do_rt,
                                                         do_sched);
          case 0b1110:
            return update_route<true, true, true, false>(k, r, do_rt,
                                                         do_sched);
          case 0b1111:
            return update_route<true, true, true, true>(k, r, do_rt,
                                                        do_sched);
          default: std::unreachable();
        }
      }();
    });
    return any_marked;
  }

  template <bool WithClaszFilter,
            bool WithBikeFilter,
            bool WithCarFilter,
            bool WithWheelchairFilter,
            bool WithReservationNotRequiredFilter>
  bool loop_rt_routes(unsigned const k) {
    auto any_marked = false;
    state_.rt_transport_mark_.for_each_set_bit([&](auto const rt_t_idx) {
      auto const rt_t = rt_transport_idx_t{rt_t_idx};

      if constexpr (WithClaszFilter) {
        if (!is_allowed(allowed_claszes_,
                        rtt_->rt_transport_section_clasz_[rt_t][0])) {
          return;
        }
      }

      auto filters = static_cast<uint8_t>(0);

      auto const apply_filter = [&](route_flag const f) {
        auto const flag_set_on_all_sections =
            rtt_->rt_transport_flags_[f].test(rt_t_idx * 2);
        if (!flag_set_on_all_sections) {
          auto const flag_set_on_some_sections =
              rtt_->rt_transport_flags_[f].test(rt_t_idx * 2 + 1);
          if (!flag_set_on_some_sections) {
            return false;
          }
          filters |=
              static_cast<uint8_t>(1 << (route_flag::kNumRouteFlags - f - 1));
          return true;
        }
        return true;
      };

      if constexpr (WithBikeFilter) {
        if (!apply_filter(route_flag::kBikesAllowed)) {
          return;
        }
      }

      if constexpr (WithCarFilter) {
        if (!apply_filter(route_flag::kCarsAllowed)) {
          return;
        }
      }

      if constexpr (WithWheelchairFilter) {
        if (!apply_filter(route_flag::kWheelchairAccessible)) {
          return;
        }
      }

      if constexpr (WithReservationNotRequiredFilter) {
        if (!apply_filter(route_flag::kReservationNotRequired)) {
          return;
        }
      }

      ++stats_.n_routes_visited_;
      trace("┊ ├k={} updating rt transport {}\n", k, rt_t);

      any_marked |= [&]() {
        switch (filters) {
          case 0b0000:
            return update_rt_transport<false, false, false, false>(k, rt_t);
          case 0b0001:
            return update_rt_transport<false, false, false, true>(k, rt_t);
          case 0b0010:
            return update_rt_transport<false, false, true, false>(k, rt_t);
          case 0b0011:
            return update_rt_transport<false, false, true, true>(k, rt_t);
          case 0b0100:
            return update_rt_transport<false, true, false, false>(k, rt_t);
          case 0b0101:
            return update_rt_transport<false, true, false, true>(k, rt_t);
          case 0b0110:
            return update_rt_transport<false, true, true, false>(k, rt_t);
          case 0b0111:
            return update_rt_transport<false, true, true, true>(k, rt_t);
          case 0b1000:
            return update_rt_transport<true, false, false, false>(k, rt_t);
          case 0b1001:
            return update_rt_transport<true, false, false, true>(k, rt_t);
          case 0b1010:
            return update_rt_transport<true, false, true, false>(k, rt_t);
          case 0b1011:
            return update_rt_transport<true, false, true, true>(k, rt_t);
          case 0b1100:
            return update_rt_transport<true, true, false, false>(k, rt_t);
          case 0b1101:
            return update_rt_transport<true, true, false, true>(k, rt_t);
          case 0b1110:
            return update_rt_transport<true, true, true, false>(k, rt_t);
          case 0b1111:
            return update_rt_transport<true, true, true, true>(k, rt_t);
          default: std::unreachable();
        }
      }();
    });
    return any_marked;
  }

  // Slot accessors: ForSched selects between the rt-slot storage (tmp_,
  // best_, round_times_, time_at_dest_) and its scheduled-slot counterpart
  // (only meaningful for RtMode == rt_mode::both). The mark sets follow the
  // same split and are written through mark_station<ForSched>(), see
  // raptor_state.h.
  template <bool ForSched>
  auto& get_tmp() {
    if constexpr (ForSched) {
      return tmp_sched_;
    } else {
      return tmp_;
    }
  }

  template <bool ForSched>
  auto& get_best() {
    if constexpr (ForSched) {
      return best_sched_;
    } else {
      return best_;
    }
  }

  template <bool ForSched>
  auto& get_round_times() {
    if constexpr (ForSched) {
      return round_times_sched_;
    } else {
      return round_times_;
    }
  }

  template <bool ForSched>
  auto& get_time_at_dest() {
    if constexpr (ForSched) {
      return time_at_dest_sched_;
    } else {
      return time_at_dest_;
    }
  }

  template <bool ForSched = false>
  void update_time_at_dest(unsigned const k, delta_t const t) {
    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }
    auto& time_at_dest = get_time_at_dest<ForSched>();
    for (auto i = k; i != time_at_dest.size(); ++i) {
      time_at_dest[i] = get_best(time_at_dest[i], t);
    }
  }

  // Applies one footpath-style improvement (transfer, static/td footpath) to
  // a single slot's best_/round_times_/time_at_dest_/station_mark_, and
  // update_time_at_dest<ForSched>() if it reaches target_v's destination.
  // ForSched selects rt vs. scheduled storage; n_earliest_arrival_updated_
  // by_footpath_ is only tracked for the rt slot, matching the previous
  // hand-duplicated code (fp_update_prevented_by_lower_bound_ counts both).
  template <bool ForSched>
  void try_update_target(unsigned const k, std::size_t const target,
                         unsigned const target_v, delta_t const target_time,
                         bool const is_dest_target) {
    auto& best = get_best<ForSched>();
    auto& round_times = get_round_times<ForSched>();
    auto& time_at_dest = get_time_at_dest<ForSched>();

    if (bounds_last_k_ == 0U && is_better(target_time, best[target][target_v])) {
      round_times[k][target][target_v] =
          get_best(target_time, round_times[k][target][target_v]);
    }

    if (!is_better(target_time, best[target][target_v]) ||
        !is_better_loose(target_time, time_at_dest[k])) {
      return;
    }
    if (!lb_reachable(target) ||
        !is_better_loose(target_time + dir(get_lb(target)), time_at_dest[k])) {
      ++stats_.fp_update_prevented_by_lower_bound_;
      return;
    }
    if (!within_bounds(k, target, target_time, target_v)) {
      return;
    }

    if constexpr (!ForSched) {
      ++stats_.n_earliest_arrival_updated_by_footpath_;
    }
    round_times[k][target][target_v] = target_time;
    best[target][target_v] = target_time;
    mark_station<ForSched>(target);
    if (is_dest_target) {
      update_time_at_dest<ForSched>(k, target_time);
    }
  }

  template <bool ForSched>
  void update_transfer(unsigned const k, std::uint64_t const i,
                            unsigned const v, [[maybe_unused]] bool const is_via,
                            bool const is_dest, unsigned const target_v,
                            delta_t const transfer_time,
                            duration_t const stay) {
    auto& tmp = get_tmp<ForSched>();
    auto const tmp_time = tmp[i][v];
    if (tmp_time == kInvalid) {
      return;
    }

    if constexpr (!ForSched) {
      trace(
          "  loc={}, v={}, tmp={}, is_dest={}, is_via={}, target_v={}, "
          "stay={}\n",
          loc{tt_, location_idx_t{i}}, v, to_unix(tmp_time), is_dest, is_via,
          target_v, stay);
    }

    auto const fp_target_time =
        clamp(tmp_time + transfer_time + dir(stay.count()));

    if constexpr (!ForSched) {
      trace("    transfer_time={}, fp_target_time={}\n", transfer_time,
            to_unix(fp_target_time));
    }

    try_update_target<ForSched>(k, i, target_v, fp_target_time, is_dest);
  }

  void update_transfers(unsigned const k) {
    for_each_prev_marked([&](std::uint64_t const i, bool const do_rt,
                             [[maybe_unused]] bool const do_sched) {
      for (auto v = 0U; v != Vias + 1; ++v) {
        // Schedule-independent (is_via_/via_stops_/transfer_time_settings_
        // are shared), so computed once and reused by both slots below.
        auto const is_via = v != Vias && is_via_[v][i];
        auto const target_v = is_via ? v + 1 : v;
        auto const is_dest = target_v == Vias && is_dest_[i];
        auto const stay = is_via ? via_stops_[v].stay_ : 0_minutes;
        auto const transfer_time =
            (!is_intermodal_dest() && is_dest)
                ? 0
                : dir(adjusted_transfer_time(
                      transfer_time_settings_,
                      tt_.locations_.transfer_time_[location_idx_t{i}]
                          .count()));

        if (do_rt) {
          update_transfer<false>(k, i, v, is_via, is_dest, target_v,
                                 transfer_time, stay);
        }
        if constexpr (RtMode == rt_mode::both) {
          if (do_sched) {
            update_transfer<true>(k, i, v, is_via, is_dest, target_v,
                                  transfer_time, stay);
          }
        }
      }
    });
  }

  template <bool ForSched>
  void update_footpath(unsigned const k, std::uint64_t const i,
                            unsigned const v, std::size_t const target,
                            unsigned const target_v, duration_t const stay,
                            footpath const& fp) {
    auto& tmp = get_tmp<ForSched>();
    auto const tmp_time = tmp[i][v];
    if (tmp_time == kInvalid) {
      return;
    }

    auto const fp_target_time = clamp(
        tmp_time + dir(adjusted_transfer_time(transfer_time_settings_,
                                              fp.duration().count()) +
                       stay.count()));
    auto const is_dest_target = target_v == Vias && is_dest_[target];
    try_update_target<ForSched>(k, target, target_v, fp_target_time,
                                is_dest_target);
  }

  void update_footpaths(unsigned const k) {
    for_each_prev_marked([&](std::uint64_t const i, bool const do_rt,
                             [[maybe_unused]] bool const do_sched) {
      auto const l_idx = location_idx_t{i};

      // Skip the rt slot here either because it did not reach this location
      // this round, or because td_footpaths -- an rtt_-only concept -- take
      // over for it (update_td_offsets()). The sched slot always uses the
      // static footpath graph below and is unaffected either way.
      auto const skip_rt = !do_rt || [&] {
        if constexpr (RtMode != rt_mode::off) {
          return prf_idx_ != 0U &&
                 (kFwd ? rtt_->has_td_footpaths_out_
                       : rtt_->has_td_footpaths_in_)[prf_idx_]
                     .test(l_idx);
        } else {
          return false;
        }
      }();

      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx_][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx_][l_idx];

      for (auto const& fp : fps) {
        auto const target = to_idx(fp.target());

        if (!skip_rt) {
          ++stats_.n_footpaths_visited_;
        }

        for (auto v = 0U; v != Vias + 1; ++v) {
          // Schedule-independent, so computed once and reused by both slots.
          auto const start_is_via =
              v != Vias && is_via_[v][static_cast<bitvec::size_type>(i)];
          auto const start_v = start_is_via ? v + 1 : v;
          auto const target_is_via =
              start_v != Vias && is_via_[start_v][target];
          auto const target_v = target_is_via ? start_v + 1 : start_v;
          auto stay = 0_minutes;
          if (start_is_via) {
            stay += via_stops_[v].stay_;
          }
          if (target_is_via) {
            stay += via_stops_[start_v].stay_;
          }

          if (!skip_rt) {
            update_footpath<false>(k, i, v, target, target_v, stay, fp);
          }
          if constexpr (RtMode == rt_mode::both) {
            if (do_sched) {
              update_footpath<true>(k, i, v, target, target_v, stay, fp);
            }
          }
        }
      }
    });
  }

  void update_td_offsets(unsigned const k) {
    if constexpr (RtMode == rt_mode::off) {
      return;
    }

    if (prf_idx_ == 0U) {
      return;
    }

    // td footpaths and tmp_ are both rt-slot only, so this walks the rt
    // slot's own marks -- the sched slot never enters here.
    state_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      if (!(kFwd ? rtt_->has_td_footpaths_out_
                 : rtt_->has_td_footpaths_in_)[prf_idx_]
               .test(l_idx)) {
        return;
      }

      auto const& fps = kFwd ? rtt_->td_footpaths_out_[prf_idx_][l_idx]
                             : rtt_->td_footpaths_in_[prf_idx_][l_idx];

      for (auto v = 0U; v != Vias + 1; ++v) {
        auto const tmp_time = tmp_[i][v];
        if (tmp_time == kInvalid) {
          continue;
        }
        for_each_footpath<
            SearchDir>(fps, to_unix(tmp_time), [&](footpath const fp) {
          ++stats_.n_footpaths_visited_;

          auto const target = to_idx(fp.target());

          auto const start_is_via =
              v != Vias && is_via_[v][static_cast<bitvec::size_type>(i)];
          auto const start_v = start_is_via ? v + 1 : v;

          auto const target_is_via =
              start_v != Vias && is_via_[start_v][target];
          auto const target_v = target_is_via ? start_v + 1 : start_v;
          auto stay = 0_minutes;
          if (start_is_via) {
            stay += via_stops_[v].stay_;
          }
          if (target_is_via) {
            stay += via_stops_[start_v].stay_;
          }

          auto const fp_target_time =
              clamp(tmp_time + dir(fp.duration().count() + stay.count()));

          if (bounds_last_k_ == 0U &&
              is_better(fp_target_time, best_[target][target_v])) {
            round_times_[k][target][target_v] =
                get_best(fp_target_time, round_times_[k][target][target_v]);
          }

          if (is_better(fp_target_time, best_[target][target_v]) &&
              is_better_loose(fp_target_time, time_at_dest_[k])) {
            if (!lb_reachable(target) ||
                !is_better_loose(fp_target_time + dir(get_lb(target)),
                                 time_at_dest_[k])) {
              ++stats_.fp_update_prevented_by_lower_bound_;
              trace_upd(
                  "┊ ├k={} *** LB NO TD FP UPD: (from={}, tmp={}) --{}--> "
                  "(to={}, best={}) --> update => {}, LB={}, LB_AT_DEST={}, "
                  "DEST={}\n",
                  k, loc{tt_, l_idx}, to_unix(tmp_[to_idx(l_idx)][v]),
                  fp.duration(), loc{tt_, fp.target()}, best_[target][target_v],
                  fp_target_time, get_lb(target),
                  to_unix(clamp(fp_target_time + dir(get_lb(target)))),
                  to_unix(time_at_dest_[k]));
              return utl::cflow::kContinue;
            }
            if (!within_bounds(k, target, fp_target_time, target_v)) {
              return utl::cflow::kContinue;
            }

            trace_upd(
                "┊ ├k={}   td footpath: ({}, tmp={}) --{}--> ({}, best={}) --> "
                "update => {}, v={}->{}, stay={}\n",
                k, loc{tt_, l_idx}, to_unix(tmp_[to_idx(l_idx)][v]),
                fp.duration(), loc{tt_, fp.target()},
                to_unix(best_[target][target_v]), to_unix(fp_target_time), v,
                target_v, stay);

            ++stats_.n_earliest_arrival_updated_by_footpath_;
            round_times_[k][target][target_v] = fp_target_time;
            best_[target][target_v] = fp_target_time;
            mark_station<false>(target);
            if (is_dest_[target]) {
              update_time_at_dest(k, fp_target_time);
            }
          } else {
            trace(
                "┊ ├k={}   NO TD FP UPDATE: {} [best={}] --{}--> {} "
                "[best={}, time_at_dest={}]\n",
                k, loc{tt_, l_idx}, best_[to_idx(l_idx)][v],
                adjusted_transfer_time(transfer_time_settings_, fp.duration()),
                loc{tt_, fp.target()}, best_[target][v],
                to_unix(time_at_dest_[k]));
          }

          return utl::cflow::kContinue;
        });
      }
    });
  }

  // td_it: iterator into td_dist_to_end_ (only dereferenced when has_td).
  template <bool ForSched>
  void update_intermodal_footpath(unsigned const k, std::uint64_t const i,
                                       bool const has_static,
                                       bool const has_td,
                                       auto const& td_it) {
    auto& tmp = get_tmp<ForSched>();
    auto& best = get_best<ForSched>();
    auto& round_times = get_round_times<ForSched>();
    auto& time_at_dest = get_time_at_dest<ForSched>();

    if (has_static) {
      // Case 1: l is last via -> add stay
      if constexpr (Vias != 0U) {
        constexpr auto v = Vias - 1U;
        if (tmp[i][v] != kInvalid && is_via_[v][i]) {
          auto const end_time = clamp(tmp[i][v]  //
                                      + dir(via_stops_[v].stay_.count())  //
                                      + dir(dist_to_end_[i]));
          if (is_better(end_time, time_at_dest[k])) {
            round_times[k][kIntermodalTarget][Vias] = end_time;
            best[kIntermodalTarget][Vias] = end_time;
            update_time_at_dest<ForSched>(k, end_time);
          }
        }
      }
    }

    // Case 2: l is no via -> don't add stay. Also the start time for the
    // td-footpath case below, since both read the same tmp[i][Vias].
    auto const tmp_time = tmp[i][Vias];
    if (has_static && tmp_time != kInvalid) {
      auto const end_time = clamp(tmp_time + dir(dist_to_end_[i]));
      if (is_better(end_time, time_at_dest[k])) {
        round_times[k][kIntermodalTarget][Vias] = end_time;
        best[kIntermodalTarget][Vias] = end_time;
        update_time_at_dest<ForSched>(k, end_time);
      }
    }
    if (has_td && tmp_time != kInvalid) {
      auto const fp =
          get_td_duration<SearchDir>(td_it->second, to_unix(tmp_time));
      if (fp.has_value()) {
        auto const& [duration, _] = *fp;
        auto const end_time = clamp(tmp_time + dir(duration.count()));
        if (is_better(end_time, time_at_dest[k]) &&
            is_better(end_time, best[kIntermodalTarget][Vias])) {
          round_times[k][kIntermodalTarget][Vias] = end_time;
          best[kIntermodalTarget][Vias] = end_time;
          update_time_at_dest<ForSched>(k, end_time);
        }
      }
    }
  }

  void update_intermodal_footpaths(unsigned const k) {
    if (dist_to_end_.empty()) {
      return;
    }

    // end_reachable_/dist_to_end_/td_dist_to_end_ are all query-level and
    // schedule-independent (static distances resp. q.td_dest_), so they're
    // shared between slots; only the tmp_/round_times_/best_/time_at_dest_
    // storage differs.
    for_each_prev_marked([&](std::uint64_t const i, bool const do_rt,
                             [[maybe_unused]] bool const do_sched) {
      if (!end_reachable_.test(i)) {
        return;
      }

      auto const l = location_idx_t{i};
      auto const has_static =
          dist_to_end_[i] != std::numeric_limits<std::uint16_t>::max();
      auto const td_it = td_dist_to_end_.find(l);
      auto const has_td = td_it != end(td_dist_to_end_);

      if (do_rt) {
        update_intermodal_footpath<false>(k, i, has_static, has_td, td_it);
      }
      if constexpr (RtMode == rt_mode::both) {
        if (do_sched) {
          update_intermodal_footpath<true>(k, i, has_static, has_td, td_it);
        }
      }
    });
  }

  template <bool WithSectionBikeFilter,
            bool WithSectionCarFilter,
            bool WithSectionWheelchairFilter,
            bool WithSectionReservationNotRequiredFilter>
  bool update_rt_transport(unsigned const k, rt_transport_idx_t const rt_t) {
    auto const stop_seq = rtt_->rt_transport_location_seq_[rt_t];
    // et[v] = there is an entry point on this transport such that the
    // journey has visited v via stops when the transport passes this stop
    auto et = std::array<bool, Vias + 1>{};
    auto any_marked = false;
    auto const n_stops = stop_seq.size();

    for (auto i = 0U; i != n_stops; ++i) {
      auto const stop_idx =
          static_cast<stop_idx_t>(kFwd ? i : n_stops - i - 1U);
      auto const stp = stop{stop_seq[stop_idx]};
      auto const l_idx = cista::to_idx(stp.location_idx());
      auto const is_first = i == 0U;
      auto const is_last = i == n_stops - 1U;

      auto const apply_filter = [&](route_flag const f) {
        if (!is_first &&
            !rtt_->rt_flags_per_section_[f][rt_t]
                                        [kFwd ? stop_idx - 1 : stop_idx]) {
          et.fill(false);
        }
      };

      if constexpr (WithSectionBikeFilter) {
        apply_filter(route_flag::kBikesAllowed);
      }

      if constexpr (WithSectionCarFilter) {
        apply_filter(route_flag::kCarsAllowed);
      }

      if constexpr (WithSectionWheelchairFilter) {
        apply_filter(route_flag::kWheelchairAccessible);
      }

      if constexpr (WithSectionReservationNotRequiredFilter) {
        apply_filter(route_flag::kReservationNotRequired);
      }

      if ((kFwd && stop_idx != 0U) || (kBwd && stop_idx != n_stops - 1U)) {
        // passing a no-stay via stop moves the ride up one via slot
        if constexpr (Vias != 0U) {
          for (auto v = Vias; v != 0U; --v) {
            if (et[v - 1U] && is_via_[v - 1U][l_idx] &&
                via_stops_[v - 1U].stay_ == 0_minutes) {
              et[v] = true;
              et[v - 1U] = false;
            }
          }
        }

        auto const by_transport = rt_time_at_stop(
            rt_t, stop_idx, kFwd ? event_type::kArr : event_type::kDep);
        for (auto j = 0U; j != Vias + 1; ++j) {
          auto const v = Vias - j;
          if (et[v] && stp.can_finish<SearchDir>(is_wheelchair_)) {
            auto current_best = get_best(round_times_[k - 1][l_idx][v],
                                         tmp_[l_idx][v], best_[l_idx][v]);

            if (is_better_loose(by_transport, time_at_dest_[k]) &&
                lb_reachable(l_idx) &&
                is_better_loose(by_transport + dir(get_lb(l_idx)),
                                time_at_dest_[k]) &&
                within_bounds(k, l_idx, by_transport, v)) {
              trace_upd(
                  "┊ │k={}    RT | name={}, dbg={}, "
                  "time_by_transport={}, "
                  "BETTER THAN current_best={} => update, {} marking station "
                  "{}!\n",
                  k, rtt_->default_trip_short_name(tt_, rt_t),
                  rtt_->dbg(tt_, rt_t), to_unix(by_transport),
                  to_unix(current_best),
                  !is_better(by_transport, current_best) ? "NOT" : "",
                  loc{tt_, stp.location_idx()});

              ++stats_.n_earliest_arrival_updated_by_route_;
              tmp_[l_idx][v] = get_best(by_transport, tmp_[l_idx][v]);
              mark_station<false>(l_idx);
              if (is_better(by_transport, current_best)) {
                current_best = by_transport;
              }
              any_marked = true;
            }
          }
        }
      }

      if (!lb_reachable(l_idx)) {
        break;
      }

      if (is_last || !(stp.can_start<SearchDir>(is_wheelchair_)) ||
          !state_.prev_station_mark_[l_idx]) {
        continue;
      }

      auto const by_transport = rt_time_at_stop(
          rt_t, stop_idx, kFwd ? event_type::kDep : event_type::kArr);
      for (auto v = 0U; v != Vias + 1; ++v) {
        auto const prev_round_time = round_times_[k - 1][l_idx][v];
        if (is_better_or_eq(prev_round_time, by_transport)) {
          et[v] = true;
        }
      }
    }
    return any_marked;
  }

  // do_rt/do_sched say which slots marked this route (see
  // for_each_marked_route()). Both set means one walk of the stop sequence
  // serves both; only one set means the other slot has no label to board from
  // and its per-stop work is skipped entirely. For rt_mode::off/on there is
  // only the rt slot and it always runs.
  template <bool WithSectionBikeFilter,
            bool WithSectionCarFilter,
            bool WithSectionWheelchairFilter,
            bool WithSectionReservationNotRequiredFilter>
  bool update_route(unsigned const k, route_idx_t const r,
                    [[maybe_unused]] bool const do_rt,
                    [[maybe_unused]] bool const do_sched) {
    auto const scan_rt = RtMode != rt_mode::both || do_rt;
    auto const stop_seq = tt_.route_location_seq_[r];
    bool any_marked = false;

    auto et = std::array<std::array<transport, Vias + 1>, Vias + 1>{};
    auto et_sched = std::array<std::array<transport, Vias + 1>, Vias + 1>{};
    auto const n_stops = stop_seq.size();

    // Applies the "riders currently on board this route arrive here" step for
    // one slot: et_arr[e][cs - e] holds, for each via state, the transport a
    // rider boarded with e vias already visited. ForSched selects rt vs.
    // scheduled storage; n_earliest_arrival_updated_by_route_ and the
    // by_transport sentinel assert() are only tracked for the rt slot,
    // matching the previous hand-duplicated code.
    auto const update_arrival = [&](auto const for_sched, stop const& stp,
                                   stop_idx_t const stop_idx,
                                   std::size_t const l_idx,
                                   std::array<std::array<transport, Vias + 1>,
                                              Vias + 1> const& et_arr,
                                   std::array<delta_t, Vias + 1>& current_best) {
      constexpr auto ForSched = decltype(for_sched)::value;
      auto& tmp = get_tmp<ForSched>();
      auto& best = get_best<ForSched>();
      auto& round_times = get_round_times<ForSched>();
      auto& time_at_dest = get_time_at_dest<ForSched>();

      for (auto j = 0U; j != Vias + 1; ++j) {
        auto const cs = Vias - j;  // current via state, descending
        for (auto e = 0U; e != cs + 1U; ++e) {
          auto const& ride = et_arr[e][cs - e];
          if (!ride.is_valid() || !stp.can_finish<SearchDir>(is_wheelchair_)) {
            continue;
          }

          auto const by_transport = time_at_stop(
              r, ride, stop_idx, kFwd ? event_type::kArr : event_type::kDep);

          if (current_best[cs] == kInvalid) {
            current_best[cs] = get_best(round_times[k - 1][l_idx][cs],
                                        tmp[l_idx][cs], best[l_idx][cs]);
          }

          if constexpr (!ForSched) {
            assert(by_transport != std::numeric_limits<delta_t>::min() &&
                   by_transport != std::numeric_limits<delta_t>::max());
          }

          if (is_better_loose(by_transport, time_at_dest[k]) &&
              lb_reachable(l_idx) &&
              is_better_loose(by_transport + dir(get_lb(l_idx)),
                              time_at_dest[k]) &&
              within_bounds(k, l_idx, by_transport, cs)) {
            if constexpr (!ForSched) {
              ++stats_.n_earliest_arrival_updated_by_route_;
            }
            tmp[l_idx][cs] = get_best(by_transport, tmp[l_idx][cs]);
            mark_station<ForSched>(l_idx);
            if (is_better(by_transport, current_best[cs])) {
              current_best[cs] = by_transport;
            }
            any_marked = true;
          }
        }
      }
    };

    auto const dep_ev = kFwd ? event_type::kDep : event_type::kArr;

    // The label a slot would look for a fresh boarding from at this stop:
    // its own round-(k-1) arrival, but only if that beats the departure of
    // the ride it already carries in et_arr[v][0] (reported back through
    // `carried`). kInvalid means this slot has nothing to board with here.
    auto const boarding_start = [&](auto const for_sched,
                                    stop_idx_t const stop_idx,
                                    std::size_t const l_idx, unsigned const v,
                                    auto const& et_arr, delta_t& carried) {
      constexpr auto ForSched = decltype(for_sched)::value;
      auto const& fresh = et_arr[v][0];
      carried = fresh.is_valid()
                    ? time_at_stop(r, fresh, stop_idx, dep_ev)
                    : kInvalid;
      auto const prev_round_time = get_round_times<ForSched>()[k - 1][l_idx][v];
      return prev_round_time != kInvalid &&
                     is_better_or_eq(prev_round_time, carried)
                 ? prev_round_time
                 : kInvalid;
    };

    // Installs the transport found for a slot as its fresh ride, if it is an
    // improvement over the one carried.
    auto const accept_boarding = [&](auto const for_sched,
                                     stop_idx_t const stop_idx,
                                     std::size_t const l_idx, unsigned const v,
                                     auto& et_arr,
                                     std::array<delta_t, Vias + 1>&
                                         current_best,
                                     delta_t const carried,
                                     transport const new_et) {
      constexpr auto ForSched = decltype(for_sched)::value;
      current_best[v] = get_best(current_best[v], get_best<ForSched>()[l_idx][v],
                                 get_tmp<ForSched>()[l_idx][v]);
      if (new_et.is_valid() &&
          (current_best[v] == kInvalid ||
           is_better_or_eq(time_at_stop(r, new_et, stop_idx, dep_ev),
                           carried))) {
        et_arr[v][0] = new_et;
      }
    };

    // Applies the "board a fresh transport here" step. Both slots are handled
    // together: fresh boardings from a slot-v label always enter rider (v, 0)
    // -- carried riders (e < v with crossings) continue unaffected -- and
    // wherever both slots are looking for one, the two searches are the same
    // walk over the same route at the same stop, so they are answered by a
    // single get_earliest_transports() call.
    auto const update_boarding = [&](stop const& stp,
                                     stop_idx_t const stop_idx,
                                     std::size_t const l_idx,
                                     bool const board_rt,
                                     [[maybe_unused]] bool const board_sched,
                                     std::array<delta_t, Vias + 1>&
                                         current_best,
                                     [[maybe_unused]]
                                     std::array<delta_t, Vias + 1>&
                                         current_best_sched) {
      auto const l = stp.location_idx();

      for (auto v = 0U; v != Vias + 1; ++v) {
        auto carried = kInvalid;
        auto const rt_start =
            board_rt ? boarding_start(std::false_type{}, stop_idx, l_idx, v,
                                      et, carried)
                     : kInvalid;

        auto carried_sched = kInvalid;
        auto sched_start = kInvalid;
        if constexpr (RtMode == rt_mode::both) {
          if (board_sched) {
            sched_start = boarding_start(std::true_type{}, stop_idx, l_idx, v,
                                         et_sched, carried_sched);
          }
        }

        auto new_et = transport{};
        auto new_et_sched = transport{};
        if constexpr (RtMode == rt_mode::both) {
          if (rt_start != kInvalid && sched_start != kInvalid) {
            std::tie(new_et, new_et_sched) =
                get_earliest_transports<true, true>(k, r, stop_idx, rt_start,
                                                    sched_start, l);
          } else if (sched_start != kInvalid) {
            new_et_sched = get_earliest_transports<false, true>(
                               k, r, stop_idx, kInvalid, sched_start, l)
                               .second;
          } else if (rt_start != kInvalid) {
            new_et = get_earliest_transports<true, false>(k, r, stop_idx,
                                                          rt_start, kInvalid, l)
                         .first;
          }
        } else if (rt_start != kInvalid) {
          new_et = get_earliest_transports<true, false>(k, r, stop_idx,
                                                        rt_start, kInvalid, l)
                       .first;
        }

        if (rt_start != kInvalid) {
          accept_boarding(std::false_type{}, stop_idx, l_idx, v, et,
                          current_best, carried, new_et);
        }
        if constexpr (RtMode == rt_mode::both) {
          if (sched_start != kInvalid) {
            accept_boarding(std::true_type{}, stop_idx, l_idx, v, et_sched,
                            current_best_sched, carried_sched, new_et_sched);
          }
        }
      }
    };

    for (auto i = 0U; i != n_stops; ++i) {
      auto const stop_idx =
          static_cast<stop_idx_t>(kFwd ? i : n_stops - i - 1U);
      auto const stp = stop{stop_seq[stop_idx]};
      auto const l_idx = cista::to_idx(stp.location_idx());
      auto const is_first = i == 0U;
      auto const is_last = i == n_stops - 1U;

      auto const apply_filter = [&](route_flag const f) {
        if (!is_first && !tt_.route_flags_per_section_[f][r][kFwd ? stop_idx - 1
                                                                  : stop_idx]) {
          et = {};
          if constexpr (RtMode == rt_mode::both) {
            et_sched = {};
          }
        }
      };

      if constexpr (WithSectionBikeFilter) {
        apply_filter(route_flag::kBikesAllowed);
      }

      if constexpr (WithSectionCarFilter) {
        apply_filter(route_flag::kCarsAllowed);
      }

      if constexpr (WithSectionWheelchairFilter) {
        apply_filter(route_flag::kWheelchairAccessible);
      }

      if constexpr (WithSectionReservationNotRequiredFilter) {
        apply_filter(route_flag::kReservationNotRequired);
      }

      // Moves carried-along riders across a no-stay via stop. Shared logic
      // (via structure is schedule-independent); run once per slot's own et.
      auto const cross_vias = [&](auto& slot_et) {
        if constexpr (Vias != 0U) {
          for (auto e = 0U; e != Vias; ++e) {
            for (auto o = Vias - e; o != 0U; --o) {
              auto& from = slot_et[e][o - 1U];
              auto const cs = e + o - 1U;  // via state before the crossing
              if (from.is_valid() && is_via_[cs][l_idx] &&
                  via_stops_[cs].stay_ == 0_minutes) {
                auto& to = slot_et[e][o];
                if (!to.is_valid() ||
                    is_better(time_at_stop(r, from, stop_idx,
                                           kFwd ? event_type::kArr
                                                : event_type::kDep),
                              time_at_stop(r, to, stop_idx,
                                          kFwd ? event_type::kArr
                                               : event_type::kDep))) {
                  to = from;
                }
                from = {};
              }
            }
          }
        }
      };
      cross_vias(et);
      if constexpr (RtMode == rt_mode::both) {
        cross_vias(et_sched);
      }

      auto current_best = std::array<delta_t, Vias + 1>{};
      current_best.fill(kInvalid);
      if (scan_rt) {
        update_arrival(std::false_type{}, stp, stop_idx, l_idx, et,
                       current_best);
      }

      auto current_best_sched = std::array<delta_t, Vias + 1>{};
      if constexpr (RtMode == rt_mode::both) {
        current_best_sched.fill(kInvalid);
        if (do_sched) {
          update_arrival(std::true_type{}, stp, stop_idx, l_idx, et_sched,
                         current_best_sched);
        }
      }

      // Each slot boards only where it has a label of its own from the
      // previous round; the stop is worth looking at as long as either does.
      auto const marked = state_.prev_station_mark_[l_idx];
      auto const marked_sched = [&] {
        if constexpr (RtMode == rt_mode::both) {
          return state_.prev_station_mark_sched_[l_idx];
        } else {
          return false;
        }
      }();

      if (is_last || !stp.can_start<SearchDir>(is_wheelchair_) ||
          (!marked && !marked_sched)) {
        continue;
      }

      if (!lb_reachable(l_idx)) {
        break;
      }

      update_boarding(stp, stop_idx, l_idx, scan_rt && marked,
                      do_sched && marked_sched, current_best,
                      current_best_sched);
    }
    return any_marked;
  }

  // Finds the earliest boardable transport of route `r` at `stop_idx` for one
  // or both slots in a single walk.
  //
  // The candidates -- the route's transports in departure order at this stop
  // -- are the same list for both slots. Only three things differ: where each
  // slot enters the list (its own round-(k-1) label), which bound it prunes
  // against (time_at_dest_ vs. time_at_dest_sched_) and which activity it
  // asks about (rtt_-aware vs. static-only). So everything the walk actually
  // costs -- seeking into the event times, stepping them, resolving the
  // transport and its start day -- is done once, and each candidate is then
  // offered to whichever slots are still looking. When the two slots arrive
  // at similar times, which is the normal case (the rt label being the
  // scheduled one plus a delay), they settle on the same candidate and the
  // second scan is gone; when they diverge, the walk simply runs as long as
  // the slower slot needs and the faster one drops out early.
  //
  // WithRt/WithSched say which slots take part; the other slot's start is
  // ignored and its result comes back invalid. rt_mode::off/on only ever
  // instantiate <true, false>, which is the single-slot scan unchanged.
  template <bool WithRt, bool WithSched>
  std::pair<transport, transport> get_earliest_transports(
      unsigned const k,
      route_idx_t const r,
      stop_idx_t const stop_idx,
      delta_t const rt_start,
      delta_t const sched_start,
      location_idx_t const l) {
    static_assert(WithRt || WithSched);
    stats_.n_earliest_trip_calls_ +=
        (WithRt ? 1U : 0U) + (WithSched ? 1U : 0U);

    auto rt_res = transport{};
    auto sched_res = transport{};
    auto rt_open = WithRt;
    auto sched_open = WithSched;

    // A slot that does not take part is pinned to the other one's start, so
    // it never widens the day window below.
    auto const rt_at = split(WithRt ? rt_start : sched_start);
    auto const sched_at = split(WithSched ? sched_start : rt_start);
    auto const [rt_day, rt_mam] = rt_at;
    auto const [sched_day, sched_mam] = sched_at;

    // The walk starts wherever the slot that can leave first starts; a slot
    // that arrives later skips the candidates it cannot reach through its own
    // gate below. Seeking by the earlier one is safe for both: a candidate
    // the later slot could take departs no earlier than one the earlier slot
    // could take.
    auto const [anchor_day, anchor_mam] =
        is_better(rt_start, sched_start) && WithRt ? rt_at : sched_at;

    auto const event_times = tt_.event_times_at_stop(
        r, stop_idx, kFwd ? event_type::kDep : event_type::kArr);

    auto const seek_first_day = [&]() {
      return linear_lb(get_begin_it(event_times), get_end_it(event_times),
                       anchor_mam,
                       [&](delta const a, minutes_after_midnight_t const b) {
                         return is_better(a.mam(), b.count());
                       });
    };

    trace("┊ │k={}    et: current_best_at_stop={}, stop_idx={}, location={}\n",
          k, tt_.to_unixtime(anchor_day, anchor_mam), stop_idx,
          loc{tt_, stop{tt_.route_location_seq_[r][stop_idx]}.location_idx()});

    // Days counted in search direction: >0 is past the slot's arrival day,
    // <0 is before it (only possible for the slot that is not the anchor).
    auto const days_after = [&](day_idx_t const from, day_idx_t const day) {
      return kFwd ? as_int(day) - as_int(from) : as_int(from) - as_int(day);
    };

    auto const lb_at_l = get_lb(to_idx(l));
    auto const n_days_to_iterate =
        static_cast<int>(kMaxTravelTime / std::chrono::days{1} + 1U);
    // Each slot still gets its own full day window, so a slot that starts
    // later than the anchor keeps the walk going past the anchor's window.
    auto const max_i = static_cast<day_idx_t::value_t>(
        std::max(days_after(anchor_day, rt_day),
                 days_after(anchor_day, sched_day)) +
        n_days_to_iterate);

    for (auto i = day_idx_t::value_t{0U}; i != max_i; ++i) {
      auto const day = kFwd ? anchor_day + i : anchor_day - i;
      auto const rt_off = days_after(rt_day, day);
      auto const sched_off = days_after(sched_day, day);

      if (rt_off >= n_days_to_iterate) {
        rt_open = false;
      }
      if (sched_off >= n_days_to_iterate) {
        sched_open = false;
      }
      if (!rt_open && !sched_open) {
        break;
      }

      if (!tt_.is_route_active(r, day)) {
        continue;
      }

      auto const ev_time_range =
          it_range{i == 0U ? seek_first_day() : get_begin_it(event_times),
                   get_end_it(event_times)};
      if (ev_time_range.empty()) {
        continue;
      }

      for (auto it = begin(ev_time_range); it != end(ev_time_range); ++it) {
        auto const t_offset =
            static_cast<std::size_t>(&*it - event_times.data());
        auto const ev = *it;
        auto const ev_mam = ev.mam();

        // Shared: the candidate's absolute departure. Only the bound it is
        // held against is per slot, and since departures only get later from
        // here, a slot that fails it is done for good.
        auto const dep = to_delta(day, ev_mam) + dir(lb_at_l);
        auto const prune = [&](auto const for_sched, bool& open,
                               int const off) {
          constexpr auto ForSched = decltype(for_sched)::value;
          if (open && off >= 0 &&
              !is_better_loose(dep, get_time_at_dest<ForSched>()[k])) {
            trace(
                "┊ │k={}      => name={}, dbg={}, day={}={}, best_mam={}, "
                "transport_mam={}, transport_time={} => TIME AT DEST {} IS "
                "BETTER!\n",
                k, tt_.transport_name(tt_.route_transport_ranges_[r][t_offset]),
                tt_.dbg(tt_.route_transport_ranges_[r][t_offset]), day,
                tt_.to_unixtime(day, 0_minutes), anchor_mam, ev_mam,
                tt_.to_unixtime(day, duration_t{ev_mam}),
                to_unix(get_time_at_dest<ForSched>()[k]));
            open = false;
          }
        };
        if constexpr (WithRt) {
          prune(std::false_type{}, rt_open, rt_off);
        }
        if constexpr (WithSched) {
          prune(std::true_type{}, sched_open, sched_off);
        }
        if (!rt_open && !sched_open) {
          return {rt_res, sched_res};
        }

        // Shared: which transport this candidate is, and which day it started
        // on -- the same answer for both slots.
        auto const t = tt_.route_transport_ranges_[r][t_offset];
        auto const start_day =
            static_cast<day_idx_t>(as_int(day) - ev.days());

        // Per slot: can it still make this departure, and does the transport
        // run for it. That is all that is left to do twice.
        auto const offer = [&](auto const for_sched, bool& open,
                               transport& res, int const off,
                               minutes_after_midnight_t const mam) {
          constexpr auto ForSched = decltype(for_sched)::value;
          if (!open || off < 0) {
            return;
          }
          if (off == 0 && !is_better_or_eq(mam.count(), ev_mam)) {
            trace(
                "┊ │k={}      => transport={}, name={}, dbg={}, day={}/{}, "
                "best_mam={}, transport_mam={}, transport_time={} => NO "
                "REACH!\n",
                k, t, tt_.transport_name(t), tt_.dbg(t), off, day, mam, ev_mam,
                ev);
            return;
          }
          auto const active = [&] {
            if constexpr (ForSched) {
              return tt_.is_transport_active(t, start_day);
            } else {
              return is_transport_active(t, start_day);
            }
          }();
          if (!active) {
            trace(
                "┊ │k={}      => transport={}, name={}, dbg={}, day={}/{}, "
                "ev_day_offset={}, best_mam={}, transport_mam={}, "
                "transport_time={} => NO TRAFFIC!\n",
                k, t, tt_.transport_name(t), tt_.dbg(t), off, day, ev.days(),
                mam, ev_mam, ev);
            return;
          }
          trace(
              "┊ │k={}      => ET FOUND: name={}, dbg={}, at day {} "
              "(day_offset={}) - ev_mam={}, ev_time={}, ev={}\n",
              k, tt_.transport_name(t), tt_.dbg(t), day, ev.days(), ev_mam, ev,
              tt_.to_unixtime(day, duration_t{ev_mam}));
          res = {t, start_day};
          open = false;
        };
        if constexpr (WithRt) {
          offer(std::false_type{}, rt_open, rt_res, rt_off, rt_mam);
        }
        if constexpr (WithSched) {
          offer(std::true_type{}, sched_open, sched_res, sched_off, sched_mam);
        }
        if (!rt_open && !sched_open) {
          return {rt_res, sched_res};
        }
      }
    }
    return {rt_res, sched_res};
  }

  bool is_transport_active(transport_idx_t const t, day_idx_t const day) const {
    if constexpr (RtMode != rt_mode::off) {
      return rtt_->is_transport_active(t, day);
    } else {
      return tt_.is_transport_active(t, day);
    }
  }

  delta_t time_at_stop(route_idx_t const r,
                       transport const t,
                       stop_idx_t const stop_idx,
                       event_type const ev_type) {
    return to_delta(t.day_,
                    tt_.event_mam(r, t.t_idx_, stop_idx, ev_type).count());
  }

  delta_t rt_time_at_stop(rt_transport_idx_t const rt_t,
                          stop_idx_t const stop_idx,
                          event_type const ev_type) {
    return to_delta(rtt_->base_day_idx_,
                    rtt_->event_time(rt_t, stop_idx, ev_type));
  }

  delta_t to_delta(day_idx_t const day, std::int16_t const mam) {
    return clamp((as_int(day) - as_int(base_)) * 1440 + mam);
  }

  unixtime_t to_unix(delta_t const t) { return delta_to_unix(base(), t); }

  std::pair<day_idx_t, minutes_after_midnight_t> split(delta_t const x) {
    return split_day_mam(base_, x);
  }

  bool is_intermodal_dest() const { return !dist_to_end_.empty(); }

  int as_int(day_idx_t const d) const { return static_cast<int>(d.v_); }

  template <typename T>
  auto get_begin_it(T const& t) {
    if constexpr (kFwd) {
      return t.begin();
    } else {
      return t.rbegin();
    }
  }

  template <typename T>
  auto get_end_it(T const& t) {
    if constexpr (kFwd) {
      return t.end();
    } else {
      return t.rend();
    }
  }

  timetable const& tt_;
  rt_timetable const* rtt_{nullptr};
  int n_days_;
  std::uint32_t n_locations_, n_routes_, n_rt_transports_;
  raptor_state& state_;
  bitvec end_reachable_;
  std::span<std::array<delta_t, Vias + 1>> tmp_;
  std::span<std::array<delta_t, Vias + 1>> best_;
  flat_matrix_view<std::array<delta_t, Vias + 1>> round_times_;
  // Scheduled-slot counterparts, used by rt_mode::both only.
  std::span<std::array<delta_t, Vias + 1>> tmp_sched_;
  std::span<std::array<delta_t, Vias + 1>> best_sched_;
  flat_matrix_view<std::array<delta_t, Vias + 1>> round_times_sched_;
  bitvec const& is_dest_;
  std::array<bitvec, kMaxVias> const& is_via_;
  std::vector<std::uint16_t> const& dist_to_end_;
  hash_map<location_idx_t, std::vector<td_offset>> const& td_dist_to_end_;
  std::vector<std::uint16_t> const& lb_;
  std::vector<via_stop> const& via_stops_;
  std::array<delta_t, kMaxTransfers + 2> time_at_dest_;
  std::array<delta_t, kMaxTransfers + 2> time_at_dest_sched_;
  day_idx_t base_;
  raptor_stats stats_;
  flat_matrix_view<std::array<delta_t, Vias + 1U> const> bounds_;
  unsigned bounds_last_k_{0U};
  profile_idx_t prf_idx_{0U};
  clasz_mask_t allowed_claszes_;
  bool require_bike_transport_;
  bool require_car_transport_;
  bool no_compulsory_reservation_;
  bool is_wheelchair_;
  transfer_time_settings transfer_time_settings_;
};

}  // namespace nigiri::routing
