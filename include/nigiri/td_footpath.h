#pragma once

#include <cstdlib>
#include <iterator>

#include <span>

#include "cista/reflection/comparable.h"

#include "utl/cflow.h"
#include "utl/equal_ranges_linear.h"
#include "utl/pairwise.h"

#include "nigiri/constants.h"
#include "nigiri/footpath.h"
#include "nigiri/routing/limits.h"
#include "nigiri/types.h"

namespace nigiri {

constexpr auto const kNull = unixtime_t{0_minutes};

// Default for the optional per-entry counter of the lookup functions below.
// Compiles to nothing; td-replay passes a counting functor instead.
struct td_no_count {
  void operator()() const noexcept {}
};

#ifdef NIGIRI_TD_TRACE
// Measurement builds only: called once per get_td_duration with the address of
// the sequence's first entry, so a recorder can tell which stop was evaluated.
using td_trace_fn = void (*)(void* ctx,
                             void const* first,
                             direction,
                             unixtime_t);
inline thread_local td_trace_fn td_trace_hook = nullptr;
inline thread_local void* td_trace_ctx = nullptr;
#endif

struct td_footpath {
  CISTA_FRIEND_COMPARABLE(td_footpath)
  location_idx_t target_;
  unixtime_t valid_from_;
  duration_t duration_;
};

// Exhaustive evaluation of alpha_tilde over a sorted step function, kept as a
// test oracle and for td-replay. Not used in production.
template <direction SearchDir, typename It, typename Count = td_no_count>
std::optional<std::pair<duration_t, typename std::iterator_traits<It>::value_type>>
scan_range(It const b, It const e, unixtime_t const t, Count&& count = {}) {
  using namespace std::chrono_literals;
  using V = typename std::iterator_traits<It>::value_type;
  auto best = std::optional<std::pair<duration_t, V>>{};
  auto const cbegin = [&] { return b; };
  auto const cend = [&] { return e; };

  // An entry is valid only on [valid_from_i, valid_from_{i+1}); a kMaxDuration
  // entry closes the window. The normalized fast path can exploit that no later
  // entry beats an earlier one and return on the first hit. Here we must check
  // every window and keep the best.
  if constexpr (SearchDir == direction::kForward) {
    auto best_arr = unixtime_t::max();
    for (auto i = cbegin(); i != cend(); ++i) {
      count();
      if (i->valid_from_ - t > routing::kMaxTravelTime) {
        break;
      }
      if (i->valid_from_ >= best_arr) {
        break;  // departs no earlier than the best arrival: cannot improve
      }
      if (i->duration_ == footpath::kMaxDuration) {
        continue;
      }
      auto const next = (i + 1) == cend() ? unixtime_t::max() : (i + 1)->valid_from_;
      if (next <= t) {
        continue;  // window already over at t
      }
      auto const dep = std::max(i->valid_from_, t);
      if (dep >= next) {
        continue;  // no time left inside the window
      }
      auto const arr = dep + i->duration_;
      if (arr < best_arr) {
        best_arr = arr;
        best = std::pair{arr - t, *i};
      }
    }
  } else /* kBackward */ {
    // Latest departure that still meets the arrival deadline t.
    // Entry e is valid on [tau_e, tau_{e+1}); inside that window the latest
    // usable departure is
    //     dep_e = min(tau_{e+1} - 1, t - l_e),
    // admissible only if dep_e >= tau_e. The answer is the maximum over
    // entries, and the returned duration is t - dep (waiting at the
    // destination counts toward it).
    auto best_dep = unixtime_t::min();
    for (auto i = cbegin(); i != cend(); ++i) {
      count();
      if (i->duration_ == footpath::kMaxDuration) {
        continue;
      }
      auto const next =
          (i + 1) == cend() ? unixtime_t::max() : (i + 1)->valid_from_;
      auto latest = t - i->duration_;
      if (next != unixtime_t::max()) {
        latest = std::min(latest, unixtime_t{next - 1min});
      }
      if (latest < i->valid_from_) {
        continue;  // window admits no feasible departure
      }
      if (t - latest > routing::kMaxTravelTime) {
        continue;  // too far back to be useful
      }
      if (latest > best_dep) {
        best_dep = latest;
        best = std::pair{t - latest, *i};
      }
    }
  }
  return best;
}

template <direction SearchDir, typename Collection, typename Count = td_no_count>
std::optional<std::pair<duration_t, typename Collection::value_type>>
get_td_duration_scan(Collection const& c,
                     unixtime_t const t,
                     Count&& count = {}) {
  return scan_range<SearchDir>(cbegin(c), cend(c), t,
                               std::forward<Count>(count));
}

// Default lookup (get_td_duration): evaluate the producers' windows
// directly, with no step function in between.
//
// GTFS-Flex delivers per stop_times row a pickup/drop-off window and a
// location; the duration is MOTIS's routed travel time. So the native object is
// a triple (window, duration, mode) -- exactly add_td_window's signature. That
// function emits {from, l, mode} followed by {to, inf, mode}, extending the
// terminator instead of appending when same-mode windows overlap, so the RAW
// vector is a sequence of start/end pairs. Walking it two at a time recovers
// the offers the producer actually published:
//
//     alpha(tau) = min over offers with to > tau of ( max(from, tau) + l )
//
// which is the losslessness equation itself, and needs no ordering assumption:
// every offer is independent, so overlapping windows from competing providers
// are handled without merging them first. The result is FIFO by construction
// (a minimum of non-decreasing arrival functions), so no normalization or FIFO
// repair is needed.
//
// Each finite entry is read as valid until the next element. On a sorted step
// function (td footpaths, or a sequence that was normalized anyway) that is
// exactly the step semantics, so this lookup is correct for those as well.
template <direction SearchDir, typename Collection, typename Count = td_no_count>
std::optional<std::pair<duration_t, typename Collection::value_type>>
get_td_duration_raw_windows(Collection const& c,
                            unixtime_t const t,
                            Count&& count = {}) {
  using namespace std::chrono_literals;
  auto best =
      std::optional<std::pair<duration_t, typename Collection::value_type>>{};
  auto const b = cbegin(c), e = cend(c);
  for (auto i = b; i != e; ++i) {
    count();
    if (i->duration_ == footpath::kMaxDuration) {
      continue;  // terminator: belongs to the offer that opened before it
    }
    auto const close = i + 1;
    auto const to =
        (close == e) ? unixtime_t::max() : close->valid_from_;  // window end
    if constexpr (SearchDir == direction::kForward) {
      if (to <= t) {
        continue;  // window already over
      }
      auto const dep = std::max(i->valid_from_, t);
      if (dep >= to) {
        continue;
      }
      auto const arr = dep + i->duration_;
      if (arr - t > routing::kMaxTravelTime) {
        continue;
      }
      if (!best.has_value() || (arr - t) < best->first) {
        best = std::pair{arr - t, *i};
      }
    } else /* kBackward */ {
      // latest departure inside [from, to) that still arrives by t
      auto latest = t - i->duration_;
      if (to != unixtime_t::max()) {
        latest = std::min(latest, unixtime_t{to - 1min});
      }
      if (latest < i->valid_from_) {
        continue;
      }
      if (t - latest > routing::kMaxTravelTime) {
        continue;
      }
      if (!best.has_value() || (t - latest) < best->first) {
        best = std::pair{t - latest, *i};
      }
    }
  }
  return best;
}

// First-hit lookup, valid only on a normalized sequence (N1). Was the default
// before the raw-window lookup; kept for td-replay and tests.
template <direction SearchDir, typename Collection, typename Count = td_no_count>
std::optional<std::pair<duration_t, typename Collection::value_type>>
get_td_duration_first(Collection const& c,
                      unixtime_t const t,
                      Count&& count = {}) {
  using namespace std::chrono_literals;

  if constexpr (SearchDir == direction::kForward) {
    for (auto i = cbegin(c); i != cend(c); ++i) {
      count();
      if (i->duration_ == footpath::kMaxDuration ||
          (i->valid_from_ < t && (i + 1) != cend(c) &&
           (i + 1)->valid_from_ <= t)) {
        continue;
      }

      if (i->valid_from_ - t > routing::kMaxTravelTime) {
        break;
      }

      return std::pair{std::max(i->valid_from_, t) + i->duration_ - t, *i};
    }

  } else /* (SearchDir == direction::kBackward) */ {
    for (auto i = crbegin(c); i != crend(c); ++i) {
      count();
      if (i->duration_ == footpath::kMaxDuration ||
          i->valid_from_ + i->duration_ > t) {
        continue;
      }

      auto const latest_arr =
          i == crbegin(c)
              ? t
              : std::min(
                    t, unixtime_t{(i - 1)->valid_from_ - 1min + i->duration_});

      if (t - latest_arr > routing::kMaxTravelTime) {
        break;
      }

      return std::pair{t - (latest_arr - i->duration_), *i};
    }
  }

  return std::nullopt;
}

template <direction SearchDir, typename Collection>
std::optional<std::pair<duration_t, typename Collection::value_type>>
get_td_duration(Collection const& c, unixtime_t const t) {
#ifdef NIGIRI_TD_TRACE
  if (td_trace_hook != nullptr && cbegin(c) != cend(c)) {
    td_trace_hook(td_trace_ctx, &*cbegin(c), SearchDir, t);
  }
#endif
  return get_td_duration_raw_windows<SearchDir>(c, t);
}

template <typename Collection>
std::optional<std::pair<duration_t, typename Collection::value_type>>
get_td_duration(direction const search_dir,
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
        auto const fp = get_td_duration<SearchDir>(std::span{from, to}, t);
        if (fp.has_value()) {
          f(footpath{from->target_, fp->first});
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