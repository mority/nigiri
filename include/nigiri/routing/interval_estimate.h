#pragma once

#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

template <direction SearchDir>
struct interval_estimator {
  explicit interval_estimator(timetable const& tt, query const& q)
      : tt_{tt}, q_{q} {

    auto const start_itv = std::visit(
        utl::overloaded{
            [](unixtime_t const& ut) { return interval<unixtime_t>{ut, ut}; },
            [](interval<unixtime_t> iut) { return iut; }},
        q.start_time_);

    auto const max_interval_size =
        q.fastest_direct_ ? kShortSearchIntervalSize : kMaxSearchIntervalSize;
    max_interval_ = {
        start_itv.from_ + (start_itv.size() / 2) - max_interval_size,
        start_itv.from_ + (start_itv.size() / 2) + max_interval_size};
  }

  interval<unixtime_t> initial(interval<unixtime_t> const& itv) const {
    if (q_.min_connection_count_ == 0 ||
        (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
      return itv;
    }

    auto new_itv = itv;
    auto const ext = duration_t{q_.min_connection_count_ * 1_hours};

    if (can_extend_bad_dir(itv)) {
      if constexpr (SearchDir == direction::kForward) {
        new_itv.to_ = std::max(itv.from_ + ext, itv.to_);
      } else {
        new_itv.from_ = std::min(itv.to_ - ext, itv.from_);
      }
    }

    clamp(new_itv);

    return new_itv;
  }

  interval<unixtime_t> extension(interval<unixtime_t> const& itv,
                                 std::uint32_t const num_con_req) const {
    if (num_con_req == 0 ||
        (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
      return itv;
    }

    auto new_itv = itv;
    auto const ext = itv.size() * num_con_req;

    if (can_extend_bad_dir(itv)) {
      extend_bad_dir(new_itv, ext);
    }

    clamp(new_itv);

    return new_itv;
  }

private:
  bool can_extend_earlier(interval<unixtime_t> const& itv) const {
    return q_.extend_interval_earlier_ &&
           itv.from_ != tt_.external_interval().from_;
  }

  bool can_extend_later(interval<unixtime_t> const& itv) const {
    return q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_;
  }

  bool can_extend_bad_dir(interval<unixtime_t> const& itv) const {
    if constexpr (SearchDir == direction::kForward) {
      return can_extend_later(itv);
    } else {
      return can_extend_earlier(itv);
    }
  }

  void extend_bad_dir(interval<unixtime_t>& itv, duration_t const d) const {
    if constexpr (SearchDir == direction::kForward) {
      itv.to_ += d;
    } else {
      itv.from_ -= d;
    }
  }

  void clamp(interval<unixtime_t>& itv) const {
    itv.from_ = tt_.external_interval().clamp(itv.from_);
    itv.to_ = tt_.external_interval().clamp(itv.to_);
    itv.from_ = max_interval_.clamp(itv.from_);
    itv.to_ = max_interval_.clamp(itv.to_);
  }

  timetable const& tt_;
  query const& q_;
  interval<unixtime_t> max_interval_;
};

}  // namespace nigiri::routing