#include "nigiri/routing/lb_raptor/meetpoint_to_pattern.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

template <direction SearchDir>
std::optional<location_idx_t> reconstruct(
    timetable const& tt,
    query const& q,
    bidir_lb_raptor_state const& state,
    location_idx_t const l,
    unsigned k,
    std::vector<location_idx_t>& pattern) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  auto const round_times =
      kFwd ? state.fwd_round_times_ : state.bwd_round_times_;

  auto const find_in_prev_round =
      [&](location_idx_t const x, unsigned const k,
          std::uint16_t const time) -> std::optional<location_idx_t> {
    for (auto const r : tt.location_lb_routes_[q.prf_idx_][x]) {
      auto const& segment_layovers = tt.lb_route_times_[q.prf_idx_][r];
      auto const& seq = tt.lb_route_root_seq_[q.prf_idx_][r];

      for (auto i = 1U; i != seq.size(); ++i) {
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
          auto const segment = kFwd ? segment_layovers[(out - 1) * 2]
                                    : segment_layovers[out * 2];
          t -= segment.count();
          if (t == round_times[k - 1][l_out]) {
            return l_out;
          }
          if (0 < out && out < static_cast<std::int32_t>(seq.size()) - 1) {
            auto const layover = segment_layovers[out * 2 - 1].count();
            t -= layover;
          }
        }
      }
    }

    return std::nullopt;
  };

  auto cur = l;
  for (; k != 0; --k) {
    auto prev = std::optional<location_idx_t>{};
    for (auto const r : tt.location_lb_routes_[q.prf_idx_][cur]) {
    }
  }
}

std::vector<location_idx_t> meetpoint_to_pattern(
    timetable const& tt,
    query const& q,
    bidir_lb_raptor_state const& state,
    location_idx_t const meetpoint) {
  auto pattern = std::vector<location_idx_t>{};
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