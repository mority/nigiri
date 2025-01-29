#pragma once

#include <span>

#include "cista/reflection/comparable.h"

#include "utl/cflow.h"
#include "utl/equal_ranges_linear.h"
#include "utl/pairwise.h"

#include "nigiri/footpath.h"
#include "nigiri/types.h"

namespace nigiri {

constexpr auto const kNull = unixtime_t{0_minutes};
constexpr auto const kInfeasible =
    duration_t{std::numeric_limits<duration_t::rep>::max()};
constexpr auto const kMaxLookAhead = std::chrono::hours{24};

struct td_footpath {
  CISTA_FRIEND_COMPARABLE(td_footpath)
  location_idx_t target_;
  unixtime_t valid_from_;
  duration_t duration_;
};

template <direction SearchDir, typename Collection>
std::optional<duration_t> get_td_duration(Collection const& c,
                                          unixtime_t const t) {
  auto const r = to_range<SearchDir>(c);
  auto const from = r.begin();
  auto const to = r.end();

  using Type = std::decay_t<decltype(*from)>;

  auto min = std::optional<duration_t>{};

  if constexpr (SearchDir == direction::kForward) {

    auto const duration_with_waiting = [&](auto const td_fp) {
      auto const start = std::max(td_fp.valid_from_, t);
      auto const end = start + td_fp.duration_;
      return end - t;
    };

    for (auto const it = from; it != to; ++it) {
      if (it->valid_from_ - kMaxLookAhead > t) {
        // reached elements too far in the future
        break;
      }

      auto const next = std::next(it);
      if (it->duration_ == footpath::kMaxDuration ||
          (next != to && next->valid_from_ < t)) {
        // current element is not valid or outdated
        continue;
      }

      min = min ? std::min(*min, duration_with_waiting(*it))
                : duration_with_waiting(*it);
    }

  } else /* SearchDir == direction::kBackward */ {

    auto const duration_with_waiting = [&](auto const td_fp) {
      auto const start = std::min(td_fp.valid_from_, t);
      auto const end = start - td_fp.duration_;
      return t - end;
    };

    for (auto const it = from; it != to; ++it) {
      if (it->valid_from_ > t) {
        continue;
      }
    }
  }

  return (min && *min < footpath::kMaxDuration) ? min : std::nullopt;
}

template <typename Collection>
std::optional<duration_t> get_td_duration(direction const search_dir,
                                          Collection const& c,
                                          unixtime_t const t) {
  return search_dir == direction::kForward
             ? get_td_duration<direction::kForward>(c, t)
             : get_td_duration<direction::kBackward>(c, t);
}

template <direction SearchDir, typename Collection, typename Fn>
void for_each_footpath(Collection const& c, unixtime_t const t, Fn&& f) {
  utl::equal_ranges_linear(
      begin(c), end(c),
      [](td_footpath const& a, td_footpath const& b) {
        return a.target_ == b.target_;
      },
      [&](auto&& from, auto&& to) {
        auto const duration =
            get_td_duration<SearchDir>(std::span{from, to}, t);
        if (duration.has_value()) {
          f(footpath{from->target_, *duration});
        }
      });
}

template <typename Collection, typename Fn>
void for_each_footpath(direction const search_dir,
                       Collection const& c,
                       unixtime_t const t,
                       Fn&& f) {
  search_dir == direction::kForward
      ? for_each_footpath<direction::kForward>(c, t, std::forward<Fn>(f))
      : for_each_footpath<direction::kBackward>(c, t, std::forward<Fn>(f));
}

}  // namespace nigiri