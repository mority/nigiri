#pragma once

#include <cassert>
#include <span>

#include "nigiri/common/delta_t.h"
#include "nigiri/common/linear_lower_bound.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/raptor/debug.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor/raptor_stats.h"
#include "nigiri/routing/raptor/reconstruct.h"
#include "nigiri/routing/transfer_time_settings.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

enum class search_mode { kOneToOne, kOneToAll };

template <direction SearchDir,
          bool Rt,
          via_offset_t Vias,
          search_mode SearchMode,
          bool ExtraSlot = false>
struct raptor {
  static_assert(!ExtraSlot ||
                (!Rt && Vias == 0U && SearchMode == search_mode::kOneToOne));

  using algo_state_t = raptor_state;
  using algo_stats_t = raptor_stats;

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

  // Second best statistics: count label times that are rejected only because
  // a stop keeps a single label. Observes, does not change the search.
  // compile with -DNIGIRI_SECOND_BEST_STATS to count
#if defined(NIGIRI_SECOND_BEST_STATS)
  static constexpr bool kSecondBestStats =
      !ExtraSlot && Vias == 0U && SearchMode == search_mode::kOneToOne;
#else
  static constexpr bool kSecondBestStats = false;
#endif
  static constexpr auto const kMaxSlotDelta = 255;
  static constexpr auto const kInvalidTop2 =
      top2{.t_ = {kInvalid, kInvalid}, .k_ = {0U, 0U}};
  enum class sb_rank : std::uint8_t { kFirst, kSecond, kWorse, kEqual };

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
        n_rt_transports_{Rt ? rtt->n_rt_transports() : 0U},
        state_{state.resize(n_locations_, n_routes_, n_rt_transports_)},
        tmp_{state_.get_tmp<Vias>()},
        best_{state_.get_best<Vias>()},
        round_times_{state.get_round_times<Vias>()},
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
        transfer_time_settings_{tts},
        block_routes_{n_routes_ != 0U &&
                      state_.blocked_routes_.size() == n_routes_},
        block_locations_{state_.blocked_locations_.size() == n_locations_} {
    assert(Vias == via_stops_.size());
    if constexpr (ExtraSlot) {
      utl::verify(td_dist_to_end_.empty(),
                  "extra slot: td offsets not supported");
      state_.resize_extra_slot();
    } else {
      state_.clear_extra_slot();
    }
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

  void fill_bounds(std::size_t const n_rows) {
    auto& s = state_;
    auto const n = static_cast<std::size_t>(s.n_locations_);

    auto const td_stops = rtt_ != nullptr && prf_idx_ != 0U
                              ? &(kFwd ? rtt_->has_td_footpaths_out_
                                       : rtt_->has_td_footpaths_in_)[prf_idx_]
                              : nullptr;

    auto const src = std::as_const(s).template get_round_times<Vias>();
    auto dst = s.template get_bounds<Vias>();

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

  void reset_arrivals() {
    utl::fill(time_at_dest_, kInvalid);
    round_times_.reset(kInvalidArray);
    if constexpr (ExtraSlot) {
      utl::fill(state_.round_delta_, std::uint8_t{0U});
      utl::fill(dest_round_best_, kInvalid);
      utl::fill(dest_round_delta_, std::uint8_t{0U});
    }
  }

  void next_start_time() {
    if constexpr (kSecondBestStats) {
      utl::fill(state_.sb_top_, kInvalidTop2);
      dest_top_ = kInvalidTop2;
    }
    utl::fill(best_, kInvalidArray);
    utl::fill(tmp_, kInvalidArray);
    if constexpr (ExtraSlot) {
      utl::fill(state_.best_delta_, std::uint8_t{0U});
      utl::fill(state_.tmp_delta_, std::uint8_t{0U});
    }
    utl::fill(state_.prev_station_mark_.blocks_, 0U);
    utl::fill(state_.station_mark_.blocks_, 0U);
    utl::fill(state_.route_mark_.blocks_, 0U);
    if constexpr (Rt) {
      utl::fill(state_.rt_transport_mark_.blocks_, 0U);
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
    state_.station_mark_.set(to_idx(l), true);
  }

  void execute(unixtime_t const start_time,
               std::uint8_t const max_transfers,
               unixtime_t const worst_time_at_dest,
               pareto_set<journey>& results) {
    auto const end_k = std::min(max_transfers, kMaxTransfers) + 2U;

    auto const d_worst_at_dest = unix_to_delta(base(), worst_time_at_dest);
    worst_at_dest_ = d_worst_at_dest;
    for (auto& time_at_dest : time_at_dest_) {
      time_at_dest = get_best(d_worst_at_dest, time_at_dest);
    }

    trace_print_init_state();

    for (auto k = 1U; k != end_k; ++k) {
      for (auto i = 0U; i != n_locations_; ++i) {
        for (auto v = 0U; v != Vias + 1; ++v) {
          best_[i][v] = get_best(round_times_[k][i][v], best_[i][v]);
        }
      }
      is_dest_.for_each_set_bit([&](std::uint64_t const i) {
        update_time_at_dest(k, best_[i][Vias]);
      });

      auto any_marked = false;
      state_.station_mark_.for_each_set_bit([&](std::uint64_t const i) {
        for (auto const& r : tt_.location_routes_[location_idx_t{i}]) {
          any_marked = true;
          state_.route_mark_.set(to_idx(r), true);
        }
        if constexpr (Rt) {
          for (auto const& rt_t :
               rtt_->location_rt_transports_[location_idx_t{i}]) {
            any_marked = true;
            state_.rt_transport_mark_.set(to_idx(rt_t), true);
          }
        }
      });

      if (!any_marked) {
        trace_print_state_after_round();
        break;
      }

      std::swap(state_.prev_station_mark_, state_.station_mark_);
      utl::fill(state_.station_mark_.blocks_, 0U);

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

      if constexpr (Rt) {
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

      utl::fill(state_.route_mark_.blocks_, 0U);
      utl::fill(state_.rt_transport_mark_.blocks_, 0U);

      std::swap(state_.prev_station_mark_, state_.station_mark_);
      utl::fill(state_.station_mark_.blocks_, 0U);

      if constexpr (ExtraSlot) {
        update_transfers_x(k);
        update_intermodal_footpaths_x(k);
        update_footpaths_x(k);
      } else {
        update_transfers(k);
        update_intermodal_footpaths(k);
        update_footpaths(k);
        update_td_offsets(k);
      }

      trace_print_state_after_round();
    }

    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }

    sb_finish();

    if constexpr (ExtraSlot) {
      collect_x(start_time, end_k, results);
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
  }

  void reconstruct(query const& q, journey& j) {
    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }
    trace("reconstruct({} - {}, {} transfers", j.departure_time(),
          j.arrival_time(), j.transfers_);
    reconstruct_journey<SearchDir>(tt_, rtt_, q, state_, j, base(), base_);
  }

  void reconstruct_alternatives(query const& q,
                                journey const& j,
                                std::vector<journey>& alternatives) {
    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }
    auto const before = alternatives.size();
    auto const t0 = std::chrono::steady_clock::now();
    auto const s = routing::reconstruct_alternatives<SearchDir>(
        tt_, rtt_, q, state_, j, q.n_alternatives_, alternatives, base(),
        base_);
    ++stats_.n_alt_results_;
    stats_.n_alt_journeys_ += alternatives.size() - before;
    stats_.n_alt_attempts_ += s.n_attempts_;
    stats_.n_alt_failed_ += s.n_failed_;
    stats_.n_alt_duplicates_ += s.n_duplicates_;
    stats_.n_alt_capped_ += s.n_capped_;
    if (s.min_skip_ == 1U) {
      ++stats_.n_alt_min_skip_1_;
    } else if (s.min_skip_ == 2U) {
      ++stats_.n_alt_min_skip_2_;
    } else if (s.min_skip_ == 3U) {
      ++stats_.n_alt_min_skip_3_;
    } else if (s.min_skip_ >= 4U && s.min_skip_ <= 7U) {
      ++stats_.n_alt_min_skip_4_7_;
    } else if (s.min_skip_ >= 8U) {
      ++stats_.n_alt_min_skip_ge_8_;
    }
    stats_.n_alt_micros_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
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

  bool is_blocked(std::uint32_t const l) const {
    return block_locations_ && state_.blocked_locations_.test(l);
  }

  static sb_rank sb_insert(top2& top, delta_t const x, unsigned const k) {
    if (x == top.t_[0] || x == top.t_[1]) {
      return sb_rank::kEqual;
    }
    if (is_better(x, top.t_[0])) {
      top.t_[1] = top.t_[0];
      top.k_[1] = top.k_[0];
      top.t_[0] = x;
      top.k_[0] = static_cast<std::uint8_t>(k);
      return sb_rank::kFirst;
    }
    if (is_better(x, top.t_[1])) {
      top.t_[1] = x;
      top.k_[1] = static_cast<std::uint8_t>(k);
      return sb_rank::kSecond;
    }
    return sb_rank::kWorse;
  }

  // labels of the search itself enter with round 0 = not from this round
  void sb_sync_dest(delta_t const t) {
    if (t != kInvalid && t != worst_at_dest_) {
      sb_insert(dest_top_, t, 0U);
    }
  }

  // pruning bound of a search with two labels per stop
  delta_t sb_bound() const {
    return dest_top_.t_[1] != kInvalid ? dest_top_.t_[1] : worst_at_dest_;
  }

  delta_t sb_label_time(std::uint32_t const l, delta_t const by_transport) {
    return (!is_intermodal_dest() && is_dest_[l])
               ? by_transport
               : clamp(by_transport +
                       dir(adjusted_transfer_time(
                           transfer_time_settings_,
                           tt_.locations_.transfer_time_[location_idx_t{l}]
                               .count())));
  }

  // x_bound: time the search compares to the time at destination
  // x_label: time the search would write as label at l
  void observe(unsigned const k,
               std::uint32_t const l,
               delta_t const x_bound,
               delta_t const x_label,
               bool const is_dest) {
    if constexpr (kSecondBestStats) {
      if (x_label == kInvalid || bounds_last_k_ != 0U) {
        return;
      }
      ++stats_.n_sb_candidates_;

      sb_sync_dest(time_at_dest_[k]);
      auto const passes = [&](delta_t const bound) {
        return lb_reachable(l) && is_better_loose(x_bound, bound) &&
               is_better_loose(x_bound + dir(get_lb(l)), bound);
      };
      auto const pruned = !passes(time_at_dest_[k]);
      if (pruned && !passes(sb_bound())) {
        return;
      }
      if (pruned) {
        ++stats_.n_sb_bound_relaxed_;
      }
      if (is_dest) {
        sb_insert(dest_top_, x_label, k);
      }

      auto& top = state_.sb_top_[l];
      if (best_[l][Vias] != kInvalid) {
        sb_insert(top, best_[l][Vias], 0U);
      }
      auto const evicts = top.t_[0] != kInvalid && top.k_[0] == k;
      switch (sb_insert(top, x_label, k)) {
        case sb_rank::kFirst:
          if (evicts) {
            ++stats_.n_sb_evicted_;
            ++state_.sb_evicted_[l];
          }
          break;
        case sb_rank::kSecond:
          ++stats_.n_sb_second_;
          ++state_.sb_second_[l];
          break;
        case sb_rank::kWorse: ++stats_.n_sb_worse_; break;
        case sb_rank::kEqual: ++stats_.n_sb_equal_; break;
      }
    }
  }

  void observe_intermodal_dest(unsigned const k, delta_t const x) {
    if constexpr (kSecondBestStats) {
      if (x == kInvalid || bounds_last_k_ != 0U) {
        return;
      }
      sb_sync_dest(time_at_dest_[k]);
      if (is_better_loose(x, sb_bound())) {
        sb_insert(dest_top_, x, k);
      }
    }
  }

  void sb_finish() {
    if constexpr (kSecondBestStats) {
      if (bounds_last_k_ != 0U) {
        return;
      }
      ++stats_.n_sb_executes_;
      sb_sync_dest(time_at_dest_.back());
      if (dest_top_.t_[0] == kInvalid) {
        return;
      }
      ++stats_.n_sb_dest_reached_;
      if (dest_top_.t_[1] == kInvalid) {
        return;
      }
      ++stats_.n_sb_dest_second_;
      auto const delta = std::abs(dest_top_.t_[1] - dest_top_.t_[0]);
      if (dest_top_.k_[0] != 0U && dest_top_.k_[1] != 0U) {
        auto const le_15 = delta <= 15;
        if (dest_top_.k_[1] == dest_top_.k_[0]) {
          ++stats_.n_sb_dest_same_round_;
          stats_.n_sb_dest_same_round_le_15_ += le_15;
        } else if (dest_top_.k_[1] > dest_top_.k_[0]) {
          ++stats_.n_sb_dest_more_transfers_;
          stats_.n_sb_dest_more_transfers_le_15_ += le_15;
        } else {
          ++stats_.n_sb_dest_fewer_transfers_;
          stats_.n_sb_dest_fewer_transfers_le_15_ += le_15;
        }
      }
      if (delta <= 5) {
        ++stats_.n_sb_dest_delta_le_5_;
      } else if (delta <= 15) {
        ++stats_.n_sb_dest_delta_le_15_;
      } else if (delta <= 30) {
        ++stats_.n_sb_dest_delta_le_30_;
      } else if (delta <= 60) {
        ++stats_.n_sb_dest_delta_le_60_;
      } else {
        ++stats_.n_sb_dest_delta_gt_60_;
      }
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

  template <bool WithClaszFilter,
            bool WithBikeFilter,
            bool WithCarFilter,
            bool WithWheelchairFilter,
            bool WithReservationNotRequiredFilter>
  bool loop_routes(unsigned const k) {
    auto any_marked = false;
    state_.route_mark_.for_each_set_bit([&](auto const r_idx) {
      auto const r = route_idx_t{r_idx};

      if (block_routes_ && state_.blocked_routes_.test(r_idx)) {
        return;
      }

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
          case 0b0000: return update_route<false, false, false, false>(k, r);
          case 0b0001: return update_route<false, false, false, true>(k, r);
          case 0b0010: return update_route<false, false, true, false>(k, r);
          case 0b0011: return update_route<false, false, true, true>(k, r);
          case 0b0100: return update_route<false, true, false, false>(k, r);
          case 0b0101: return update_route<false, true, false, true>(k, r);
          case 0b0110: return update_route<false, true, true, false>(k, r);
          case 0b0111: return update_route<false, true, true, true>(k, r);
          case 0b1000: return update_route<true, false, false, false>(k, r);
          case 0b1001: return update_route<true, false, false, true>(k, r);
          case 0b1010: return update_route<true, false, true, false>(k, r);
          case 0b1011: return update_route<true, false, true, true>(k, r);
          case 0b1100: return update_route<true, true, false, false>(k, r);
          case 0b1101: return update_route<true, true, false, true>(k, r);
          case 0b1110: return update_route<true, true, true, false>(k, r);
          case 0b1111: return update_route<true, true, true, true>(k, r);
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

  void update_transfers(unsigned const k) {
    state_.prev_station_mark_.for_each_set_bit([&](auto&& i) {
      for (auto v = 0U; v != Vias + 1; ++v) {
        auto const tmp_time = tmp_[i][v];
        if (tmp_time == kInvalid) {
          continue;
        }

        auto const is_via = v != Vias && is_via_[v][i];
        auto const target_v = is_via ? v + 1 : v;
        auto const is_dest = target_v == Vias && is_dest_[i];
        auto const stay = is_via ? via_stops_[v].stay_ : 0_minutes;

        trace(
            "  loc={}, v={}, tmp={}, is_dest={}, is_via={}, target_v={}, "
            "stay={}\n",
            loc{tt_, location_idx_t{i}}, v, to_unix(tmp_time), is_dest, is_via,
            target_v, stay);

        auto const transfer_time =
            (!is_intermodal_dest() && is_dest)
                ? 0
                : dir(adjusted_transfer_time(
                      transfer_time_settings_,
                      tt_.locations_.transfer_time_[location_idx_t{i}]
                          .count()));
        auto const fp_target_time =
            clamp(tmp_time + transfer_time + dir(stay.count()));

        if (bounds_last_k_ == 0U &&
            is_better(fp_target_time, best_[i][target_v])) {
          round_times_[k][i][target_v] =
              get_best(fp_target_time, round_times_[k][i][target_v]);
        }

        trace(
            "    transfer_time={}, fp_target_time={}, best@target={}, "
            "dest={}\n",
            transfer_time, to_unix(fp_target_time), to_unix(best_[i][target_v]),
            to_unix(time_at_dest_[k]));

        if (is_better(fp_target_time, best_[i][target_v]) &&
            is_better_loose(fp_target_time, time_at_dest_[k])) {
          if (!lb_reachable(i) ||
              !is_better_loose(fp_target_time + dir(get_lb(i)),
                               time_at_dest_[k])) {
            ++stats_.fp_update_prevented_by_lower_bound_;
            continue;
          }
          if (!within_bounds(k, i, fp_target_time, target_v)) {
            continue;
          }

          ++stats_.n_earliest_arrival_updated_by_footpath_;
          round_times_[k][i][target_v] = fp_target_time;
          best_[i][target_v] = fp_target_time;
          state_.station_mark_.set(i, true);
          if (is_dest) {
            update_time_at_dest(k, fp_target_time);
          }
        }
      }
    });
  }

  void update_footpaths(unsigned const k) {
    state_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      if constexpr (Rt) {
        if (prf_idx_ != 0U && (kFwd ? rtt_->has_td_footpaths_out_
                                    : rtt_->has_td_footpaths_in_)[prf_idx_]
                                  .test(l_idx)) {
          return;
        }
      }

      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx_][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx_][l_idx];

      for (auto const& fp : fps) {
        ++stats_.n_footpaths_visited_;

        auto const target = to_idx(fp.target());

        for (auto v = 0U; v != Vias + 1; ++v) {
          auto const tmp_time = tmp_[i][v];
          if (tmp_time == kInvalid) {
            continue;
          }

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

          auto const fp_target_time = clamp(
              tmp_time + dir(adjusted_transfer_time(transfer_time_settings_,
                                                    fp.duration().count()) +
                             stay.count()));

          // walking from destination to destination is no alternative
          observe(k, target, fp_target_time, fp_target_time,
                  !is_intermodal_dest() && is_dest_[target] && !is_dest_[i]);

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
                  "┊ ├k={} *** LB NO UPD: (from={}, tmp={}) --{}--> (to={}, "
                  "best={}) --> update => {}, LB={}, LB_AT_DEST={}, DEST={}\n",
                  k, loc{tt_, l_idx}, to_unix(tmp_[to_idx(l_idx)][v]),
                  adjusted_transfer_time(transfer_time_settings_,
                                         fp.duration()),
                  loc{tt_, fp.target()}, best_[target][target_v],
                  to_unix(fp_target_time), get_lb(target),
                  to_unix(clamp(fp_target_time + dir(get_lb(target)))),
                  to_unix(time_at_dest_[k]));
              continue;
            }
            if (!within_bounds(k, target, fp_target_time, target_v)) {
              continue;
            }

            trace_upd(
                "┊ ├k={}   footpath: ({}, tmp={}) --{}--> ({}, best={}) --> "
                "update => {}, v={}->{}, stay={}\n",
                k, loc{tt_, l_idx}, to_unix(tmp_[to_idx(l_idx)][v]),
                adjusted_transfer_time(transfer_time_settings_, fp.duration()),
                loc{tt_, fp.target()}, to_unix(best_[target][target_v]),
                to_unix(fp_target_time), v, target_v, stay);

            ++stats_.n_earliest_arrival_updated_by_footpath_;
            round_times_[k][target][target_v] = fp_target_time;
            best_[target][target_v] = fp_target_time;
            state_.station_mark_.set(target, true);
            if (target_v == Vias && is_dest_[target]) {
              update_time_at_dest(k, fp_target_time);
            }
          } else {
            trace(
                "┊ ├k={}   NO FP UPDATE: {} [best={}] --{}--> {} "
                "[best={}, time_at_dest={}]\n",
                k, loc{tt_, l_idx}, to_unix(best_[to_idx(l_idx)][target_v]),
                adjusted_transfer_time(transfer_time_settings_, fp.duration()),
                loc{tt_, fp.target()}, to_unix(best_[target][target_v]),
                to_unix(time_at_dest_[k]));
          }
        }
      }
    });
  }

  void update_td_offsets(unsigned const k) {
    if constexpr (!Rt) {
      return;
    }

    if (prf_idx_ == 0U) {
      return;
    }

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

          observe(k, target, fp_target_time, fp_target_time,
                  !is_intermodal_dest() && is_dest_[target] && !is_dest_[i]);

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
            state_.station_mark_.set(target, true);
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

  void update_intermodal_footpaths(unsigned const k) {
    if (dist_to_end_.empty()) {
      return;
    }

    state_.prev_station_mark_.for_each_set_bit([&](auto const i) {
      if (!end_reachable_.test(i)) {
        trace_upd("┊ ├k={}   no end_reachable: {}\n", k,
                  loc{tt_, location_idx_t{i}});
        [[likely]];
        return;
      }

      trace_upd("┊ ├k={}   end_reachable: {}\n", k,
                loc{tt_, location_idx_t{i}});

      auto const l = location_idx_t{i};
      if (dist_to_end_[i] != std::numeric_limits<std::uint16_t>::max()) {
        [[likely]];

        // Case 1: l is last via -> add stay
        if constexpr (Vias != 0U) {
          constexpr auto v = Vias - 1U;
          if (tmp_[i][v] != kInvalid && is_via_[v][i]) {
            auto const end_time = clamp(tmp_[i][v]  //
                                        + dir(via_stops_[v].stay_.count())  //
                                        + dir(dist_to_end_[i]));

            trace_upd(
                "┊ ├k={}, INTERMODAL FOOTPATH FROM LAST VIA: ({}, tmp={}) "
                "--({} +stay={})--> "
                "({}, best={})",
                k, loc{tt_, l}, to_unix(tmp_[to_idx(l)][v]), dist_to_end_[i],
                via_stops_[v].stay_,
                loc{tt_, location_idx_t{kIntermodalTarget}},
                to_unix(best_[kIntermodalTarget][Vias]), to_unix(end_time));

            if (is_better_loose(end_time, time_at_dest_[k])) {
              round_times_[k][kIntermodalTarget][Vias] = end_time;
              best_[kIntermodalTarget][Vias] = end_time;
              update_time_at_dest(k, end_time);
              trace_upd(" -> update\n");
            } else {
              trace_upd(" -> no update\n");
            }
          }
        }

        // Case 2: l is no via -> don't add stay
        auto const tmp_time = tmp_[i][Vias];
        if (tmp_time == kInvalid) {
          trace_upd("┊ ├k={}, loc={} NOT REACHED\n", k, loc{tt_, l});
          return;
        }

        auto const end_time = clamp(tmp_time + dir(dist_to_end_[i]));
        observe_intermodal_dest(k, end_time);

        trace_upd(
            "┊ ├k={}, INTERMODAL FOOTPATH: ({}, tmp={}) --{}--> "
            "({}, best={})",
            k, loc{tt_, l}, to_unix(tmp_[to_idx(l)][Vias]), dist_to_end_[i],
            loc{tt_, location_idx_t{kIntermodalTarget}},
            to_unix(best_[kIntermodalTarget][Vias]), to_unix(end_time));

        if (is_better_loose(end_time, time_at_dest_[k])) {
          round_times_[k][kIntermodalTarget][Vias] = end_time;
          best_[kIntermodalTarget][Vias] = end_time;
          update_time_at_dest(k, end_time);
          trace_upd(" -> update\n");
        } else {
          trace_upd(" -> no update\n");
        }
      }

      if (auto const it = td_dist_to_end_.find(l); it != end(td_dist_to_end_)) {
        [[unlikely]];

        auto const fp_start_time = tmp_[i][Vias];
        if (fp_start_time == kInvalid) {
          return;
        }
        auto const fp =
            get_td_duration<SearchDir>(it->second, to_unix(fp_start_time));
        if (fp.has_value()) {
          auto const& [duration, _] = *fp;
          auto const end_time = clamp(fp_start_time + dir(duration.count()));
          observe_intermodal_dest(k, end_time);

          if (is_better_loose(end_time, time_at_dest_[k]) &&
              is_better(end_time, best_[kIntermodalTarget][Vias])) {
            round_times_[k][kIntermodalTarget][Vias] = end_time;
            best_[kIntermodalTarget][Vias] = end_time;
            update_time_at_dest(k, end_time);

            trace(
                "┊ │k={}  TD INTERMODAL FOOTPATH: location={}, "
                "start_time={}, dist_to_end={} --> update to {}\n",
                k, loc{tt_, l}, to_unix(fp_start_time), duration,
                to_unix(end_time));
          } else {
            trace(
                "┊ │k={}  TD INTERMODAL FOOTPATH: location={}, "
                "start_time={}, dist_to_end={} --> NO update to {} best={}\n",
                k, loc{tt_, l}, to_unix(fp_start_time), duration,
                to_unix(end_time), best_[kIntermodalTarget][Vias]);
          }
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
          if (et[v] && stp.can_finish<SearchDir>(is_wheelchair_) &&
              !is_blocked(l_idx)) {
            auto current_best = get_best(round_times_[k - 1][l_idx][v],
                                         tmp_[l_idx][v], best_[l_idx][v]);

            observe(k, l_idx, by_transport, sb_label_time(l_idx, by_transport),
                    !is_intermodal_dest() && is_dest_[l_idx]);

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
              state_.station_mark_.set(l_idx, true);
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
          !state_.prev_station_mark_[l_idx] || is_blocked(l_idx)) {
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

  template <bool WithSectionBikeFilter,
            bool WithSectionCarFilter,
            bool WithSectionWheelchairFilter,
            bool WithSectionReservationNotRequiredFilter>
  bool update_route(unsigned const k, route_idx_t const r) {
    if constexpr (ExtraSlot) {
      return update_route_x<WithSectionBikeFilter, WithSectionCarFilter,
                            WithSectionWheelchairFilter,
                            WithSectionReservationNotRequiredFilter>(k, r);
    }

    auto const stop_seq = tt_.route_location_seq_[r];
    bool any_marked = false;

    auto et = std::array<std::array<transport, Vias + 1>, Vias + 1>{};
    auto const n_stops = stop_seq.size();

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

      if constexpr (Vias != 0U) {
        for (auto e = 0U; e != Vias; ++e) {
          for (auto o = Vias - e; o != 0U; --o) {
            auto& from = et[e][o - 1U];
            auto const cs = e + o - 1U;  // via state before the crossing
            if (from.is_valid() && is_via_[cs][l_idx] &&
                via_stops_[cs].stay_ == 0_minutes) {
              auto& to = et[e][o];
              if (!to.is_valid() ||
                  is_better(
                      time_at_stop(r, from, stop_idx,
                                   kFwd ? event_type::kArr : event_type::kDep),
                      time_at_stop(
                          r, to, stop_idx,
                          kFwd ? event_type::kArr : event_type::kDep))) {
                to = from;
              }
              from = {};
            }
          }
        }
      }

      auto current_best = std::array<delta_t, Vias + 1>{};
      current_best.fill(kInvalid);

      for (auto j = 0U; j != Vias + 1; ++j) {
        auto const cs = Vias - j;  // current via state, descending
        for (auto e = 0U; e != cs + 1U; ++e) {
          auto const& ride = et[e][cs - e];
          if (!ride.is_valid()) {
            continue;
          }
          if (!stp.can_finish<SearchDir>(is_wheelchair_) ||
              is_blocked(l_idx)) {
            trace(
                "┊ │k={} cs={}    *** NO UPD: in_allowed={}, "
                "out_allowed={}, label_allowed={}\n",
                k, cs, stp.in_allowed(), stp.out_allowed(),
                (kFwd ? stp.out_allowed() : stp.in_allowed()));
            continue;
          }
          auto const by_transport = time_at_stop(
              r, ride, stop_idx, kFwd ? event_type::kArr : event_type::kDep);

          if (current_best[cs] == kInvalid) {
            current_best[cs] = get_best(round_times_[k - 1][l_idx][cs],
                                        tmp_[l_idx][cs], best_[l_idx][cs]);
          }

          assert(by_transport != std::numeric_limits<delta_t>::min() &&
                 by_transport != std::numeric_limits<delta_t>::max());
          observe(k, l_idx, by_transport, sb_label_time(l_idx, by_transport),
                  !is_intermodal_dest() && is_dest_[l_idx]);
          if (is_better_loose(by_transport, time_at_dest_[k]) &&
              lb_reachable(l_idx) &&
              is_better_loose(by_transport + dir(get_lb(l_idx)),
                              time_at_dest_[k]) &&
              within_bounds(k, l_idx, by_transport, cs)) {
            trace_upd(
                "┊ │k={} cs={}    name={}, dbg={}, "
                "time_by_transport={}, "
                "BETTER THAN current_best={} => update, {} marking station "
                "{}!\n",
                k, cs, tt_.transport_name(ride.t_idx_), tt_.dbg(ride.t_idx_),
                to_unix(by_transport), to_unix(current_best[cs]),
                !is_better(by_transport, current_best[cs]) ? "NOT" : "",
                loc{tt_, stp.location_idx()});

            ++stats_.n_earliest_arrival_updated_by_route_;
            tmp_[l_idx][cs] = get_best(by_transport, tmp_[l_idx][cs]);
            state_.station_mark_.set(l_idx, true);
            if (is_better(by_transport, current_best[cs])) {
              current_best[cs] = by_transport;
            }
            any_marked = true;
          } else {
            trace(
                "┊ │k={} cs={}    *** NO UPD: at={}, name={}, "
                "dbg={}, "
                "time_by_transport={}, current_best={}\n",
                k, cs, loc{tt_, location_idx_t{l_idx}},
                tt_.transport_name(ride.t_idx_), tt_.dbg(ride.t_idx_),
                to_unix(by_transport), to_unix(current_best[cs]));
          }
        }
      }

      if (is_last || !stp.can_start<SearchDir>(is_wheelchair_) ||
          !state_.prev_station_mark_[l_idx] || is_blocked(l_idx)) {
        continue;
      }

      if (!lb_reachable(l_idx)) {
        break;
      }

      for (auto v = 0U; v != Vias + 1; ++v) {
        // fresh boardings from a slot-v label always enter rider (v, 0);
        // carried riders (e < v with crossings) continue unaffected
        auto& fresh = et[v][0];
        auto const et_time_at_stop =
            fresh.is_valid()
                ? time_at_stop(r, fresh, stop_idx,
                               kFwd ? event_type::kDep : event_type::kArr)
                : kInvalid;
        auto const prev_round_time = round_times_[k - 1][l_idx][v];
        if (prev_round_time != kInvalid &&
            is_better_or_eq(prev_round_time, et_time_at_stop)) {
          auto const [day, mam] = split(prev_round_time);
          auto const new_et = get_earliest_transport(k, r, stop_idx, day, mam,
                                                     stp.location_idx());
          current_best[v] =
              get_best(current_best[v], best_[l_idx][v], tmp_[l_idx][v]);
          if (new_et.is_valid() &&
              (current_best[v] == kInvalid ||
               is_better_or_eq(
                   time_at_stop(r, new_et, stop_idx,
                                kFwd ? event_type::kDep : event_type::kArr),
                   et_time_at_stop))) {
            fresh = new_et;
            trace("┊ │k={} v={}    update et: time_at_stop={}\n", k, v,
                  to_unix(et_time_at_stop));
          } else if (new_et.is_valid()) {
            trace(
                "┊ │k={} v={}    update et: no update "
                "time_at_stop={}\n",
                k, v, to_unix(et_time_at_stop));
          }
        }
      }
    }
    return any_marked;
  }

  // ===========
  // EXTRA SLOT
  // -----------
  // A label is a pair (time, delta): best time and, if delta != 0, one later
  // time = time +/- delta minutes. Round k keeps a later time x at a stop if
  //   - round k wrote the best time b of the stop and b < x <= b + 255
  //   - x is earlier than the best time of all rounds < k
  //   - x is earlier than the later time kept so far
  // Equal times are one time.

  enum class slot : std::uint8_t { kNone, kBest, kSecond };

  static delta_t second(delta_t const t, std::uint8_t const d) {
    return d == 0U ? kInvalid : clamp(t + dir(static_cast<int>(d)));
  }

  static slot insert(delta_t& t, std::uint8_t& d, delta_t const x) {
    if (x == kInvalid || x == t) {
      return slot::kNone;
    }
    if (is_better(x, t)) {
      auto const diff = t == kInvalid ? kMaxSlotDelta + 1 : std::abs(t - x);
      d = diff <= kMaxSlotDelta ? static_cast<std::uint8_t>(diff) : 0U;
      t = x;
      return slot::kBest;
    }
    auto const diff = std::abs(x - t);
    if (diff > kMaxSlotDelta || (d != 0U && diff >= d)) {
      return slot::kNone;
    }
    d = static_cast<std::uint8_t>(diff);
    return slot::kSecond;
  }

  // Latest useful arrival at the destination in round k.
  delta_t dest_bound_x(unsigned const k) const {
    auto bound = time_at_dest_[k - 1U];
    if (dest_round_best_[k] != kInvalid) {
      bound = get_best(
          bound, dest_round_delta_[k] != 0U
                     ? second(dest_round_best_[k], dest_round_delta_[k])
                     : clamp(dest_round_best_[k] + dir(kMaxSlotDelta)));
    }
    return bound;
  }

  bool prune_x(std::uint32_t const l,
               delta_t const x,
               delta_t const bound) const {
    return !lb_reachable(l) || !is_better_loose(x, bound) ||
           !is_better_loose(x + dir(get_lb(l)), bound);
  }

  // x = time of a label at l in round k (transfer time / footpath included)
  // dest: l is destination and x did not get there by a footpath from
  //       another destination
  bool insert_label_x(unsigned const k,
                      std::uint32_t const l,
                      delta_t const x,
                      bool const is_dest,
                      bool const from_dest) {
    // Labels at the destination end here. All other labels are continued
    // in the next rounds -> bound of the next round.
    if (prune_x(l, x, is_dest ? dest_bound_x(k) : time_at_dest_[k])) {
      return false;
    }

    auto& best = best_[l][0];
    auto& best_d = state_.best_delta_[l];
    auto& round = round_times_[k][l][0];
    auto& round_d = state_.round_delta(k, l);

    if (is_better(x, best)) {
      insert(best, best_d, x);
      insert(round, round_d, x);
      ++stats_.n_earliest_arrival_updated_by_footpath_;
    } else {
      // extra slot only behind a best time of this round
      if (x == best || round == kInvalid || (is_dest && from_dest)) {
        return false;
      }
      if (insert(best, best_d, x) != slot::kSecond) {
        ++stats_.n_slot_rejects_;
        return false;
      }
      round_d = best_d;
      ++stats_.n_slot_labels_;
    }

    state_.station_mark_.set(l, true);
    if (is_dest && !from_dest) {
      update_time_at_dest(k, x);
      insert(dest_round_best_[k], dest_round_delta_[k], x);
    }
    return true;
  }

  // times reached by transit at stop i in this round
  std::array<delta_t, 2> tmp_x(std::uint32_t const i) const {
    return {tmp_[i][0], second(tmp_[i][0], state_.tmp_delta_[i])};
  }

  bool is_dest_x(std::uint32_t const l) const {
    return is_intermodal_dest() ? l == kIntermodalTarget : is_dest_[l];
  }

  void update_transfers_x(unsigned const k) {
    state_.prev_station_mark_.for_each_set_bit([&](auto&& i) {
      auto const is_dest = is_dest_x(i);
      auto const transfer_time =
          (!is_intermodal_dest() && is_dest)
              ? 0
              : dir(adjusted_transfer_time(
                    transfer_time_settings_,
                    tt_.locations_.transfer_time_[location_idx_t{i}].count()));
      for (auto const t : tmp_x(i)) {
        if (t != kInvalid) {
          insert_label_x(k, i, clamp(t + transfer_time), is_dest, false);
        }
      }
    });
  }

  void update_footpaths_x(unsigned const k) {
    state_.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
      auto const l_idx = location_idx_t{i};
      auto const& fps = kFwd ? tt_.locations_.footpaths_out_[prf_idx_][l_idx]
                             : tt_.locations_.footpaths_in_[prf_idx_][l_idx];
      auto const from_dest = is_dest_x(i);
      for (auto const& fp : fps) {
        ++stats_.n_footpaths_visited_;
        auto const target = to_idx(fp.target());
        auto const duration = dir(adjusted_transfer_time(
            transfer_time_settings_, fp.duration().count()));
        for (auto const t : tmp_x(i)) {
          if (t != kInvalid) {
            insert_label_x(k, target, clamp(t + duration), is_dest_x(target),
                           from_dest);
          }
        }
      }
    });
  }

  void update_intermodal_footpaths_x(unsigned const k) {
    if (dist_to_end_.empty()) {
      return;
    }

    state_.prev_station_mark_.for_each_set_bit([&](auto const i) {
      if (!end_reachable_.test(i) || dist_to_end_[i] == kUnreachable) {
        return;
      }
      for (auto const t : tmp_x(i)) {
        if (t == kInvalid) {
          continue;
        }
        auto const x = clamp(t + dir(dist_to_end_[i]));
        if (!is_better_loose(x, dest_bound_x(k))) {
          continue;
        }
        auto& best = best_[kIntermodalTarget][0];
        auto& best_d = state_.best_delta_[kIntermodalTarget];
        auto& round = round_times_[k][kIntermodalTarget][0];
        auto& round_d = state_.round_delta(k, kIntermodalTarget);
        if (is_better(x, best)) {
          insert(best, best_d, x);
          insert(round, round_d, x);
        } else if (x == best || round == kInvalid ||
                   insert(best, best_d, x) != slot::kSecond) {
          continue;
        } else {
          round_d = best_d;
          ++stats_.n_slot_labels_;
        }
        update_time_at_dest(k, x);
        insert(dest_round_best_[k], dest_round_delta_[k], x);
      }
    });
  }

  template <bool WithSectionBikeFilter,
            bool WithSectionCarFilter,
            bool WithSectionWheelchairFilter,
            bool WithSectionReservationNotRequiredFilter>
  bool update_route_x(unsigned const k, route_idx_t const r) {
    auto const stop_seq = tt_.route_location_seq_[r];
    auto const n_stops = stop_seq.size();
    auto const bound = dest_bound_x(k);
    auto any_marked = false;

    // trips in use, earlier one first: no trip of a route overtakes another
    // one, further trips arrive third everywhere
    auto et = std::array<transport, 2>{};

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

      if (stp.can_finish<SearchDir>(is_wheelchair_) && !is_blocked(l_idx)) {
        for (auto const& ride : et) {
          if (!ride.is_valid()) {
            continue;
          }
          auto const by_transport = time_at_stop(
              r, ride, stop_idx, kFwd ? event_type::kArr : event_type::kDep);
          if (!prune_x(l_idx, by_transport, bound)) {
            ++stats_.n_earliest_arrival_updated_by_route_;
            insert(tmp_[l_idx][0], state_.tmp_delta_[l_idx], by_transport);
            state_.station_mark_.set(l_idx, true);
            any_marked = true;
          }
        }
      }

      if (is_last || !stp.can_start<SearchDir>(is_wheelchair_) ||
          !state_.prev_station_mark_[l_idx] || is_blocked(l_idx)) {
        continue;
      }

      if (!lb_reachable(l_idx)) {
        break;
      }

      auto const dep = [&](transport const t) {
        return time_at_stop(r, t, stop_idx,
                            kFwd ? event_type::kDep : event_type::kArr);
      };

      auto const prev = round_times_[k - 1U][l_idx][0];
      auto caught = kInvalid;  // departure of the trip the best label takes
      for (auto const label :
           {prev, second(prev, state_.round_delta(k - 1U, l_idx))}) {
        if (label == kInvalid ||
            // takes the trip of the best label
            (caught != kInvalid && is_better_or_eq(label, caught)) ||
            // too late for every trip that would be of use
            (et[1].is_valid() && !is_better_or_eq(label, dep(et[1])))) {
          break;
        }

        auto const [day, mam] = split(label);
        auto const t = get_earliest_transport(k, r, stop_idx, day, mam,
                                              stp.location_idx(), bound);
        if (!t.is_valid()) {
          break;
        }
        caught = dep(t);

        if (t == et[0] || t == et[1]) {
          continue;
        }
        if (!et[0].is_valid() || is_better(dep(t), dep(et[0]))) {
          et[1] = et[0];
          et[0] = t;
        } else if (!et[1].is_valid() || is_better(dep(t), dep(et[1]))) {
          et[1] = t;
        }
      }
    }
    return any_marked;
  }

  void collect_x(unixtime_t const start_time,
                 unsigned const end_k,
                 pareto_set<journey>& results) {
    auto const find_dest = [&](unsigned const k, delta_t const t) {
      auto dest = location_idx_t::invalid();
      is_dest_.for_each_set_bit([&](auto const i) {
        if (dest == location_idx_t::invalid() &&
            (round_times_[k][i][0] == t ||
             second(round_times_[k][i][0], state_.round_delta(k, i)) == t)) {
          dest = location_idx_t{i};
        }
      });
      return dest;
    };

    auto best_so_far = kInvalid;  // rounds < k
    for (auto k = 1U; k != end_k; ++k) {
      auto const best = dest_round_best_[k];
      if (best == kInvalid || !is_better(best, best_so_far)) {
        continue;
      }

      results.add(journey{.legs_ = {},
                          .start_time_ = start_time,
                          .dest_time_ = delta_to_unix(base(), best),
                          .dest_ = find_dest(k, best),
                          .transfers_ = static_cast<std::uint8_t>(k - 1)});

      auto const x = second(best, dest_round_delta_[k]);
      if (x != kInvalid && is_better(x, best_so_far)) {
        ++stats_.n_slot_journeys_;
        results.add_not_optimal(
            journey{.legs_ = {},
                    .start_time_ = start_time,
                    .dest_time_ = delta_to_unix(base(), x),
                    .dest_ = find_dest(k, x),
                    .transfers_ = static_cast<std::uint8_t>(k - 1),
                    .slot_ = 1U});
      }
      best_so_far = best;
    }
  }

  transport get_earliest_transport(unsigned const k,
                                   route_idx_t const r,
                                   stop_idx_t const stop_idx,
                                   day_idx_t const day_at_stop,
                                   minutes_after_midnight_t const mam_at_stop,
                                   location_idx_t const l) {
    return get_earliest_transport(k, r, stop_idx, day_at_stop, mam_at_stop, l,
                                  time_at_dest_[k]);
  }

  transport get_earliest_transport([[maybe_unused]] unsigned const k,
                                   route_idx_t const r,
                                   stop_idx_t const stop_idx,
                                   day_idx_t const day_at_stop,
                                   minutes_after_midnight_t const mam_at_stop,
                                   location_idx_t const l,
                                   delta_t const bound) {
    ++stats_.n_earliest_trip_calls_;

    auto const event_times = tt_.event_times_at_stop(
        r, stop_idx, kFwd ? event_type::kDep : event_type::kArr);

    auto const seek_first_day = [&]() {
      return linear_lb(get_begin_it(event_times), get_end_it(event_times),
                       mam_at_stop,
                       [&](delta const a, minutes_after_midnight_t const b) {
                         return is_better(a.mam(), b.count());
                       });
    };

    trace("┊ │k={}    et: current_best_at_stop={}, stop_idx={}, location={}\n",
          k, tt_.to_unixtime(day_at_stop, mam_at_stop), stop_idx,
          loc{tt_, stop{tt_.route_location_seq_[r][stop_idx]}.location_idx()});

    auto const n_days_to_iterate = kMaxTravelTime / std::chrono::days{1} + 1U;
    for (auto i = day_idx_t::value_t{0U}; i != n_days_to_iterate; ++i) {
      auto const day = kFwd ? day_at_stop + i : day_at_stop - i;

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

        if (!is_better_loose(to_delta(day, ev_mam) + dir(get_lb(to_idx(l))),
                             bound)) {
          trace(
              "┊ │k={}      => name={}, dbg={}, day={}={}, best_mam={}, "
              "transport_mam={}, transport_time={} => TIME AT DEST {} IS "
              "BETTER!\n",
              k, tt_.transport_name(tt_.route_transport_ranges_[r][t_offset]),
              tt_.dbg(tt_.route_transport_ranges_[r][t_offset]), day,
              tt_.to_unixtime(day, 0_minutes), mam_at_stop, ev_mam,
              tt_.to_unixtime(day, duration_t{ev_mam}),
              to_unix(time_at_dest_[k]));
          return {transport_idx_t::invalid(), day_idx_t::invalid()};
        }

        auto const t = tt_.route_transport_ranges_[r][t_offset];
        if (i == 0U && !is_better_or_eq(mam_at_stop.count(), ev_mam)) {
          trace(
              "┊ │k={}      => transport={}, name={}, dbg={}, day={}/{}, "
              "best_mam={}, "
              "transport_mam={}, transport_time={} => NO REACH!\n",
              k, t, tt_.transport_name(t), tt_.dbg(t), i, day, mam_at_stop,
              ev_mam, ev);
          continue;
        }

        auto const ev_day_offset = ev.days();
        auto const start_day =
            static_cast<day_idx_t>(as_int(day) - ev_day_offset);
        if (!is_transport_active(t, start_day)) {
          trace(
              "┊ │k={}      => transport={}, name={}, dbg={}, day={}/{}, "
              "ev_day_offset={}, "
              "best_mam={}, "
              "transport_mam={}, transport_time={} => NO TRAFFIC!\n",
              k, t, tt_.transport_name(t), tt_.dbg(t), i, day, ev_day_offset,
              mam_at_stop, ev_mam, ev);
          continue;
        }

        trace(
            "┊ │k={}      => ET FOUND: name={}, dbg={}, at day {} "
            "(day_offset={}) - ev_mam={}, ev_time={}, ev={}\n",
            k, tt_.transport_name(t), tt_.dbg(t), day, ev_day_offset, ev_mam,
            ev, tt_.to_unixtime(day, duration_t{ev_mam}));
        return {t, static_cast<day_idx_t>(as_int(day) - ev_day_offset)};
      }
    }
    return {};
  }

  bool is_transport_active(transport_idx_t const t, day_idx_t const day) const {
    if constexpr (Rt) {
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

  void update_time_at_dest(unsigned const k, delta_t const t) {
    if constexpr (SearchMode == search_mode::kOneToAll) {
      return;
    }
    for (auto i = k; i != time_at_dest_.size(); ++i) {
      time_at_dest_[i] = get_best(time_at_dest_[i], t);
    }
  }

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
  bitvec const& is_dest_;
  std::array<bitvec, kMaxVias> const& is_via_;
  std::vector<std::uint16_t> const& dist_to_end_;
  hash_map<location_idx_t, std::vector<td_offset>> const& td_dist_to_end_;
  std::vector<std::uint16_t> const& lb_;
  std::vector<via_stop> const& via_stops_;
  std::array<delta_t, kMaxTransfers + 2> time_at_dest_;
  // extra slot: best and later arrival at the destination in exactly round k
  std::array<delta_t, kMaxTransfers + 2> dest_round_best_;
  std::array<std::uint8_t, kMaxTransfers + 2> dest_round_delta_;
  day_idx_t base_;
  raptor_stats stats_;
  top2 dest_top_{kInvalidTop2};
  delta_t worst_at_dest_{kInvalid};
  flat_matrix_view<std::array<delta_t, Vias + 1U> const> bounds_;
  unsigned bounds_last_k_{0U};
  profile_idx_t prf_idx_{0U};
  clasz_mask_t allowed_claszes_;
  bool require_bike_transport_;
  bool require_car_transport_;
  bool no_compulsory_reservation_;
  bool is_wheelchair_;
  transfer_time_settings transfer_time_settings_;
  bool block_routes_;
  bool block_locations_;
};

}  // namespace nigiri::routing
