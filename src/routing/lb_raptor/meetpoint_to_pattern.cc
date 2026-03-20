#include "nigiri/routing/lb_raptor/meetpoint_to_pattern.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/lb_raptor/bidir_lb_raptor_state.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

template <direction SearchDir>
void reconstruct(timetable const& tt,
                 query const& q,
                 bidir_lb_raptor_state const& state,
                 location_idx_t const l,
                 unsigned const k_start,
                 vector<location_idx_t>& pattern) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const round_times =
      kFwd ? state.fwd_round_times_ : state.bwd_round_times_;

  auto const find_in_prev_round =
      [&](location_idx_t const x, unsigned const k,
          std::uint16_t const time) -> std::optional<location_idx_t> {
    for (auto const r : tt.location_lb_routes_[q.prf_idx_][x]) {
      auto const& seq = tt.lb_route_root_seq_[q.prf_idx_][r];

      for (auto i = 0U; i != seq.size(); ++i) {
        auto const in = kFwd ? seq.size() - i - 1U : i;
        auto const l_in = seq[in];
        if (l_in != x) {
          continue;
        }

        auto t = time;
        static constexpr auto step = kFwd ? -1 : 1;
        for (auto out = static_cast<std::int32_t>(in + step);
             0 <= out && out < static_cast<std::int32_t>(seq.size());
             out += step) {
          auto const l_out = seq[out];
          auto const segment =
              kFwd ? tt.lb_get_departing_segment(q.prf_idx_, r, out)
                   : tt.lb_get_arriving_segment(q.prf_idx_, r, out);

          t -= segment.count();
          if (t == round_times[k - 1][l_out]) {
            return l_out;
          }
          if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
            t -= tt.lb_get_layover(q.prf_idx_, r, out).count();
          }
        }
      }
    }

    return std::nullopt;
  };

  auto cur = l;
  for (auto k = k_start; k > 1U; --k) {
    auto prev = find_in_prev_round(
        cur, k,
        round_times[k][cur] -
            adjusted_transfer_time(q.transfer_time_settings_,
                                   tt.locations_.transfer_time_[cur].count()));
    if (prev) {
      pattern.emplace_back(*prev);
      cur = *prev;
      continue;
    }

    [&] {
      auto const expand_fps = [&](auto const x) {
        for (auto const fp :
             kFwd ? tt.locations_.footpaths_in_[q.prf_idx_][x]
                  : tt.locations_.footpaths_out_[q.prf_idx_][x]) {
          prev = find_in_prev_round(
              tt.locations_.get_root_idx(fp.target()), k,
              round_times[k][cur] -
                  adjusted_transfer_time(q.transfer_time_settings_,
                                         fp.duration())
                      .count());
          if (prev) {
            return;
          }
        }
      };
      expand_fps(cur);
      if (prev) {
        return;
      }
      for (auto const c : tt.locations_.children_[cur]) {
        expand_fps(c);
        if (prev) {
          return;
        }
        for (auto const cc : tt.locations_.children_[c]) {
          expand_fps(cc);
          if (prev) {
            return;
          }
        }
      }
    }();
    if (prev) {
      pattern.emplace_back(*prev);
      cur = *prev;
      continue;
    }

    fmt::println(
        "reconstruction failed, could not find matching entry in previous "
        "round");
  }
}

vector<location_idx_t> meetpoint_to_pattern(timetable const& tt,
                                            query const& q,
                                            bidir_lb_raptor_state const& state,
                                            location_idx_t const meetpoint) {
  auto pattern = vector<location_idx_t>{};

  auto const get_init_round = [&](auto const& round_times) {
    return utl::find_if(round_times,
                        [&](auto const& times) {
                          return times[meetpoint] != kUnreachable;
                        }) -
           begin(round_times);
  };

  reconstruct<direction::kForward>(
      tt, q, state, meetpoint, get_init_round(state.fwd_round_times_), pattern);

  std::ranges::reverse(pattern);
  pattern.emplace_back(meetpoint);

  reconstruct<direction::kBackward>(
      tt, q, state, meetpoint, get_init_round(state.bwd_round_times_), pattern);

  return pattern;
}

}  // namespace nigiri::routing