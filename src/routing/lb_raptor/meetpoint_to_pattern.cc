#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"

#include "nigiri/routing/journey.h"
#include "nigiri/timetable.h"

#include "utl/enumerate.h"
#include "utl/to_vec.h"

// #define trace(...)
#define trace fmt::println

namespace nigiri::routing {
constexpr auto kUnreachable = std::numeric_limits<std::uint16_t>::max();

template <direction SearchDir>
void bidir_lb_raptor::reconstruct(timetable const& tt,
                                  query const& q,
                                  location_idx_t const l,
                                  unsigned const k_start) {
  static constexpr auto kFwd = SearchDir == direction::kForward;
  auto const& round_times = kFwd ? fwd_round_times_ : bwd_round_times_;
  auto const& is_terminal = kFwd ? is_start_ : is_dest_;

  auto const find_in_prev_round =
      [&](location_idx_t const x, unsigned const k,
          std::uint16_t const time) -> std::optional<location_idx_t> {
    trace("[find_in_prev_round][{}] location={}, round={}, time={}",
          kFwd ? "fwd" : "bwd", tt.get_default_name(x), k, time);

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
    auto const time = round_times[k][cur];

    auto const local_transfer =
        [&](location_idx_t const x) -> std::optional<location_idx_t> {
      return find_in_prev_round(
          x, k,
          time -
              adjusted_transfer_time(q.transfer_time_settings_,
                                     tt.locations_.transfer_time_[x].count()));
    };

    auto const footpath_transfer =
        [&](location_idx_t const x) -> std::optional<location_idx_t> {
      auto const expand_fps = [&](auto const y) {
        auto found = std::optional<location_idx_t>{};
        for (auto const fp :
             kFwd ? tt.locations_.footpaths_in_[q.prf_idx_][y]
                  : tt.locations_.footpaths_out_[q.prf_idx_][y]) {
          found = find_in_prev_round(
              tt.locations_.get_root_idx(fp.target()), k,
              time - adjusted_transfer_time(q.transfer_time_settings_,
                                            fp.duration())
                         .count());
          if (found) {
            break;
          }
        }
        return found;
      };

      auto found = expand_fps(x);
      if (found) {
        return found;
      }
      for (auto const c : tt.locations_.children_[x]) {
        found = expand_fps(c);
        if (found) {
          return found;
        }
        for (auto const cc : tt.locations_.children_[c]) {
          found = expand_fps(cc);
          if (found) {
            return found;
          }
        }
      }

      return std::nullopt;
    };

    auto prev = local_transfer(cur);
    if (!prev) {
      prev = footpath_transfer(cur);
    }
    if (!prev) {
      trace(
          "[reconstruct][{}] failed k={}, cur={}, could not find matching "
          "entry in previous round,",
          kFwd ? "fwd" : "bwd", k, cur, tt.get_default_name(cur));
      return;
    }

    if (is_terminal.test(to_idx(*prev))) {
      trace(
          "[reconstruct][{}] reached terminal {}, terminating "
          "reconstruction",
          kFwd ? "fwd" : "bwd", tt.get_default_name(*prev));
      return;
    }

    trace("[reconstruct][{}] adding {} to pattern", kFwd ? "fwd" : "bwd",
          tt.get_default_name(*prev));
    current_pattern_.emplace_back(*prev);
    cur = *prev;
  }
}

template <direction SearchDir>
void bidir_lb_raptor::meetpoints_to_patterns(timetable const& tt,
                                             query const& q,
                                             unsigned const k) {
  static constexpr auto kFwd = SearchDir == direction::kForward;

  if constexpr (kFwd) {
    if (k == 1 && !meetpoints_.empty()) {
      trace("[meetpoints_to_patterns][{}] possible direct connection",
            kFwd ? "fwd" : "bwd");
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
    trace("[meetpoints_to_patterns][{}] meetpoint: {}", kFwd ? "fwd" : "bwd",
          tt.get_default_name(m));
    if (is_start_.test(to_idx(m)) || is_dest_.test(to_idx(m))) {
      trace("[meetpoints_to_patterns][{}] skipping terminal",
            kFwd ? "fwd" : "bwd");
      continue;
    }

    current_pattern_.clear();
    reconstruct<direction::kForward>(tt, q, m, k);
    std::ranges::reverse(current_pattern_);
    current_pattern_.emplace_back(m);
    reconstruct<direction::kBackward>(tt, q, m, kFwd ? k - 1 : k);

    if (patterns_.emplace(to_array(current_pattern_)).second) {
      trace("[meetpoints_to_patterns][{}] new pattern: {}",
            kFwd ? "fwd" : "bwd",
            utl::to_vec(current_pattern_,
                        [&](auto const l) { return tt.get_default_name(l); }));
    } else {
      trace("[meetpoints_to_patterns][{}] pattern repetition",
            kFwd ? "fwd" : "bwd");
    }
  }
}

template void bidir_lb_raptor::meetpoints_to_patterns<direction::kForward>(
    timetable const&, query const&, unsigned);
template void bidir_lb_raptor::meetpoints_to_patterns<direction::kBackward>(
    timetable const&, query const&, unsigned);

}  // namespace nigiri::routing