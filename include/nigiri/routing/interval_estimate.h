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
                                 std::uint32_t const num_con_req) const {
    auto new_itv = itv;
    auto const ext = itv.size() * num_con_req;

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
  bool can_extend_both_dir(interval<unixtime_t> const& itv) const {
    return q_.extend_interval_earlier_ &&
           itv.from_ != tt_.external_interval().from_ &&
           q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_;
  }

  timetable const& tt_;
  query const& q_;
};

}  // namespace nigiri::routing