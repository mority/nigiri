#pragma once

#include "nigiri/types.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

constexpr auto const step = i32_minutes{60};
constexpr auto const half_step = step / 2;

template <direction SearchDir>
struct interval_estimator {
  explicit interval_estimator(timetable const& tt, query const& q)
      : tt_{tt}, q_{q} {}

  interval<unixtime_t> initial(interval<unixtime_t> const& itv) const {
    auto new_itv = itv;

    if (q_.min_connection_count_ == 0 ||
        (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
      return new_itv;
    }

    // extend interval into allowed directions
    if (can_extend_both_dir(new_itv)) {
      new_itv.from_ -= half_step * q_.min_connection_count_;
      new_itv.to_ += half_step * q_.min_connection_count_;
    } else {
      if (q_.extend_interval_earlier_) {
        new_itv.from_ -= step * q_.min_connection_count_;
      }
      if (q_.extend_interval_later_) {
        new_itv.to_ += step * q_.min_connection_count_;
      }
    }

    // clamp to timetable
    new_itv.from_ = tt_.external_interval().clamp(new_itv.from_);
    new_itv.to_ = tt_.external_interval().clamp(new_itv.to_);

    return new_itv;
  }

  interval<unixtime_t> extension(interval<unixtime_t> const& itv,
                                 pareto_set<journey> const& results,
                                 std::uint32_t const num_con_req) const {
    auto new_itv = itv;
    if (max_itv_reached(new_itv)) {
      return new_itv;
    }

    auto n_j_with_legs = 0U;
    for (auto const& j : results) {
      if (!j.legs_.empty()) {
        ++n_j_with_legs;
      }
    }

    auto ext = i32_minutes{0U};
    if (n_j_with_legs != 0) {
      auto const time_per_con = itv.size() / n_j_with_legs;
      ext = time_per_con * num_con_req;
    } else {
      ext = itv.size() * num_con_req * 2;
    }

    // extend
    if (can_extend_both_dir(new_itv)) {
      new_itv.from_ -= ext / 2;
      new_itv.to_ += ext / 2;
    } else {
      if (q_.extend_interval_earlier_) {
        new_itv.from_ -= ext;
      }
      if (q_.extend_interval_later_) {
        new_itv.to_ += ext;
      }
    }

    // clamp to timetable
    new_itv.from_ = tt_.external_interval().clamp(new_itv.from_);
    new_itv.to_ = tt_.external_interval().clamp(new_itv.to_);

    return new_itv;
  }

private:
  bool max_itv_reached(interval<unixtime_t> const& itv) const {
    auto const can_search_earlier = q_.extend_interval_earlier_ &&
                                    itv.from_ != tt_.external_interval().from_;
    auto const can_search_later =
        q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_;
    return !can_search_earlier && !can_search_later;
  }

  bool can_extend_both_dir(interval<unixtime_t> const& itv) const {
    return q_.extend_interval_earlier_ &&
           itv.from_ != tt_.external_interval().from_ &&
           q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_;
  }

  timetable const& tt_;
  query const& q_;
};

}  // namespace nigiri::routing