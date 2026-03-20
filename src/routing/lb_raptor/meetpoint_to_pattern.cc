#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include "nigiri/routing/journey.h"
#include "nigiri/timetable.h"

#include "utl/enumerate.h"

namespace nigiri::routing {
constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

template <direction SearchDir>
void bidir_lb_raptor::reconstruct(timetable const& tt,
                                  query const& q,
                                  location_idx_t const l,
                                  unsigned const k_start) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const round_times = kFwd ? fwd_round_times_ : bwd_round_times_;

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
      current_pattern_.emplace_back(*prev);
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
      current_pattern_.emplace_back(*prev);
      cur = *prev;
      continue;
    }

    fmt::println(
        "reconstruction failed, could not find matching entry in previous "
        "round");
  }
}

template <direction SearchDir>
void bidir_lb_raptor::meetpoints_to_patterns(timetable const& tt,
                                             query const& q,
                                             unsigned const k) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  if constexpr (kFwd) {
    if (k == 1 && meetpoints_.size() > 0) {
      fmt::println("possible direct connection");
    }
    return;
  }

  auto const to_array = [&](auto const& v) {
    auto a = std::array<location_idx_t, kMaxTransfers>{};
    utl::fill(a, location_idx_t::invalid());
    for (auto const [i, e] : utl::enumerate(v)) {
      a[i] = e;
    }
    return a;
  };

  for (auto const m : meetpoints_) {
    fmt::print("meetpoint: {}, ", tt.get_default_name(m));

    current_pattern_.clear();
    reconstruct<direction::kForward>(tt, q, m, k);
    std::ranges::reverse(current_pattern_);
    current_pattern_.emplace_back(m);
    reconstruct<direction::kBackward>(tt, q, m, kFwd ? k - 1 : k);

    if (patterns_.emplace(to_array(current_pattern_)).second) {
      fmt::print("new pattern:");
      for (auto const l : current_pattern_) {
        fmt::print(" {}", tt.get_default_name(l));
      }
      fmt::print("\n");
    } else {
      fmt::println("pattern repetition");
    }
  }
}

template void bidir_lb_raptor::meetpoints_to_patterns<direction::kForward>(
    timetable const&, query const&, unsigned);
template void bidir_lb_raptor::meetpoints_to_patterns<direction::kBackward>(
    timetable const&, query const&, unsigned);

}  // namespace nigiri::routing