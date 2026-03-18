#include "nigiri/routing/bidir_lb_raptor.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include <vector>

#include "utl/get_or_create.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

struct meet_point {
  location_idx_t l_;
  bool fwd_;
  std::uint8_t k_;
};

void bidir_lb_raptor_state::reset(unsigned const n_locations,
                                  unsigned const n_lb_routes) {
  auto const reset_round_times = [&](auto& round_times) {
    for (auto& a : round_times) {
      a.resize(n_locations);
      utl::fill(a, kUnreachable);
    }
  };
  reset_round_times(fwd_round_times_);
  reset_round_times(bwd_round_times_);

  tmp_.resize(n_locations);
  utl::fill(tmp_, kUnreachable);

  auto const reset_bitvec = [&](auto& v, auto const size) {
    v.resize(size);
    utl::fill(v.blocks_, 0U);
  };

  reset_bitvec(fwd_station_mark_, n_locations);
  reset_bitvec(bwd_station_mark_, n_locations);
  reset_bitvec(prev_station_mark_, n_locations);
  reset_bitvec(fwd_reached_, n_locations);
  reset_bitvec(bwd_reached_, n_locations);
  reset_bitvec(lb_route_mark_, n_lb_routes);
  reset_bitvec(lb_route_reached_, n_lb_routes);

  tps_.clear();
}

template <direction SearchDir>
void init(timetable const& tt, query const& q, bidir_lb_raptor_state& state) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const& offsets = kFwd ? q.start_ : q.destination_;
  auto const& td_offsets = kFwd ? q.td_start_ : q.td_dest_;
  auto const match_mode = kFwd ? q.start_match_mode_ : q.dest_match_mode_;
  auto& round_times = kFwd ? state.fwd_round_times_ : state.bwd_round_times_;
  auto& station_mark = kFwd ? state.fwd_station_mark_ : state.bwd_station_mark_;
  auto& reached = kFwd ? state.fwd_reached_ : state.bwd_reached_;

  state.min_.clear();
  auto const update_min = [&](location_idx_t const l, std::uint16_t const d) {
    auto const root = tt.locations_.get_root_idx(l);
    auto& m = utl::get_or_create(state.min_, root,
                                 [&] { return round_times[0][root]; });
    m = std::min(d, m);
  };

  for (auto const& o : offsets) {
    for_each_meta(tt, match_mode, o.target(), [&](location_idx_t const l) {
      update_min(l, static_cast<std::uint16_t>(o.duration().count()));
    });
  }

  for (auto const& [l, tds] : td_offsets) {
    for (auto const& td : tds) {
      if (td.duration() != footpath::kMaxDuration &&
          td.duration() < q.max_travel_time_) {
        update_min(l, static_cast<std::uint16_t>(td.duration().count()));
      }
    }
  }

  for (auto const& [l, t] : state.min_) {
    for_each_meta(tt, match_mode, l, [&](location_idx_t const meta) {
      round_times[0][meta] =
          std::min(t, round_times[0][meta]);  // necessary to min again?
      station_mark.set(to_idx(meta), true);
      reached.set(to_idx(meta), true);
    });
  }
}

std::string route_str(timetable const& tt,
                      profile_idx_t const prf_idx,
                      lb_route_idx_t const r) {
  auto ss = std::stringstream{};
  ss << "[" << r << ":";
  for (auto const l : tt.lb_route_root_seq_[prf_idx][r]) {
    ss << " " << tt.get_default_name(l);
  }
  ss << "]";
  return ss.str();
}

template <direction SearchDir>
bool run(timetable const& tt,
         query const& q,
         bidir_lb_raptor_state& state,
         unsigned const k) {
  auto const& routes = tt.location_lb_routes_[q.prf_idx_];
  auto const& route_times = tt.lb_route_times_[q.prf_idx_];
  auto const& route_root_seq = tt.lb_route_root_seq_[q.prf_idx_];

  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto& round_times = kFwd ? state.fwd_round_times_ : state.bwd_round_times_;
  auto& station_mark = kFwd ? state.fwd_station_mark_ : state.bwd_station_mark_;
  auto& reached = kFwd ? state.fwd_reached_ : state.bwd_reached_;
  auto& rev_reached = kFwd ? state.bwd_reached_ : state.fwd_reached_;

  for (auto const l :
       interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
    round_times[k][l] = round_times[k - 1U][l];
  }
  utl::fill(state.tmp_, kUnreachable);

  auto any_marked = false;
  station_mark.for_each_set_bit([&](std::uint64_t const i) {
    fmt::println("{}: location {} is marked", kFwd ? "fwd" : "bwd",
                 tt.get_default_name(location_idx_t{i}));
    for (auto const r : routes[location_idx_t{i}]) {
      fmt::println("{}: marking route {}", kFwd ? "fwd" : "bwd",
                   route_str(tt, q.prf_idx_, r));
      if (!state.lb_route_reached_.test(to_idx(r))) {
        any_marked = true;
        state.lb_route_mark_.set(to_idx(r), true);
        state.lb_route_reached_.set(to_idx(r), true);
      }
    }
  });
  if (!any_marked) {
    return false;
  }

  std::swap(state.prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  any_marked = false;
  state.lb_route_mark_.for_each_set_bit([&](auto const i) {
    auto const r = lb_route_idx_t{i};
    auto const& segment_layovers = route_times[r];
    auto const& seq = route_root_seq[r];

    fmt::println("{}: route {} is marked", kFwd ? "fwd" : "bwd",
                 route_str(tt, q.prf_idx_, r));

    for (auto x = 0U; x != seq.size(); ++x) {
      auto const in = kFwd ? x : seq.size() - x - 1U;
      auto const l_in = seq[in];

      if (!state.prev_station_mark_.test(to_idx(l_in))) {
        continue;
      }

      fmt::println("{}: found entry location {}", kFwd ? "fwd" : "bwd",
                   tt.get_default_name(l_in));

      auto lb = round_times[k - 1U][l_in];
      auto const step = kFwd ? 1 : -1;
      for (auto out = static_cast<std::int32_t>(in + step);
           0 <= out && out < static_cast<std::int32_t>(seq.size());
           out += step) {
        auto const l_out = seq[out];
        auto const segment =
            kFwd ? segment_layovers[(out - 1) * 2] : segment_layovers[out * 2];

        lb += segment.count();
        fmt::println(
            "{}: reached exit location {} with lb = {}, round_times[k={}][{}] "
            "= {}, state.tmp_[{}] = {}, rev_reached = {}",
            kFwd ? "fwd" : "bwd", tt.get_default_name(l_out), lb, k,
            tt.get_default_name(l_out), round_times[k][l_out],
            tt.get_default_name(l_out), state.tmp_[l_out],
            rev_reached.test(to_idx(l_out)));
        if (lb < std::min(round_times[k][l_out], state.tmp_[l_out])) {
          state.tmp_[l_out] = lb;
          station_mark.set(to_idx(l_out), true);
          reached.set(to_idx(l_out), true);
          any_marked = true;

          fmt::println("{}: new lb, marking location {}", kFwd ? "fwd" : "bwd",
                       tt.get_default_name(l_out));

          if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
            auto const layover = segment_layovers[out * 2 - 1].count();
            lb += layover;
          } else {
            break;
          }
        } else {
          break;
        }
      }
    }
  });
  if (!any_marked) {
    return false;
  }

  utl::fill(state.lb_route_mark_.blocks_, 0U);

  std::swap(state.prev_station_mark_, station_mark);
  utl::fill(station_mark.blocks_, 0U);

  state.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const time_after_transfer =
        state.tmp_[l] +
        adjusted_transfer_time(q.transfer_time_settings_,
                               tt.locations_.transfer_time_[l].count());
    round_times[k][l] = time_after_transfer;
    station_mark.set(i, true);
    reached.set(i, true);
  });

  state.prev_station_mark_.for_each_set_bit([&](std::uint64_t const i) {
    auto const l = location_idx_t{i};

    auto const expand_fps = [&](auto const x) {
      for (auto const fp : kFwd ? tt.locations_.footpaths_out_[q.prf_idx_][x]
                                : tt.locations_.footpaths_in_[q.prf_idx_][x]) {
        auto const root = tt.locations_.get_root_idx(fp.target());
        auto const time_after_fp =
            state.tmp_[l] + adjusted_transfer_time(q.transfer_time_settings_,
                                                   fp.duration().count());
        if (time_after_fp < round_times[k][root]) {
          round_times[k][root] = time_after_fp;
          station_mark.set(to_idx(root), true);
          reached.set(to_idx(root), true);
        }
      }
    };

    expand_fps(l);
    for (auto const c : tt.locations_.children_[l]) {
      expand_fps(c);
      for (auto const cc : tt.locations_.children_[c]) {
        expand_fps(cc);
      }
    }
  });

  if (!any_marked) {
    return false;
  }

  station_mark.for_each_set_bit([&](auto const i) {
    fmt::println("{}: meetpoint check at location {}", kFwd ? "fwd" : "bwd",
                 tt.get_default_name(location_idx_t{i}));
    if (rev_reached.test(i)) {
      fmt::println("{} already reached, saving meetpoint",
                   kFwd ? "bwd" : "fwd");
      station_mark.set(i, false);
      // handle meet point -> reconstruct transfer pattern
      auto meetpoint = std::array<location_idx_t, kMaxTransfers>{};
      meetpoint[k - 1U] = location_idx_t{i};
      state.tps_.push_back(meetpoint);

      fmt::print("{}: meetpoint: ", kFwd ? "fwd" : "bwd");
      for (auto const l : state.tps_.back()) {
        fmt::print(" {}",
                   l == location_idx_t{0} ? "-" : tt.get_default_name(l));
      }
      fmt::print("\n");
    }
  });

  return station_mark.any();
}

void bidir_lb_raptor(timetable const& tt,
                     query const& q,
                     bidir_lb_raptor_state& state) {

  state.reset(tt.n_locations(), tt.lb_route_times_[q.prf_idx_].size());

  // init (k = 0)
  init<direction::kForward>(tt, q, state);
  init<direction::kBackward>(tt, q, state);

  // run
  auto run_fwd = true;
  auto run_bwd = true;
  for (auto k = 1U; k != (std::min(q.max_transfers_, kMaxTransfers) + 2U) / 2U;
       ++k) {
    fmt::println("k = {}: run_fwd: {}, run_bwd: {}", k,
                 run_fwd ? "true" : "false", run_bwd ? "true" : "false");
    if (run_fwd) {
      run_fwd = run<direction::kForward>(tt, q, state, k);
    }
    if (run_bwd) {
      run_bwd = run<direction::kBackward>(tt, q, state, k);
    }
  }
}
}  // namespace nigiri::routing