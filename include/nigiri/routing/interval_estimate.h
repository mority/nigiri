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

    data_type_max_interval_ = {
        std::chrono::round<std::chrono::days>(
            start_itv.from_ + ((start_itv.to_ - start_itv.from_) / 2)) -
            kMaxSearchIntervalSize,
        std::chrono::round<std::chrono::days>(
            start_itv.from_ + ((start_itv.to_ - start_itv.from_) / 2)) +
            kMaxSearchIntervalSize};
  }

  interval<unixtime_t> initial(interval<unixtime_t> const& itv) const {
    if (q_.min_connection_count_ == 0 ||
        (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
      return itv;
    }

    auto new_itv = itv;
    auto const ext = 1_hours * q_.min_connection_count_;

    if (can_extend_bad_dir(itv)) {
      if constexpr (SearchDir == direction::kForward) {
        if (can_extend_earlier(itv)) {
          new_itv.from_ -= 1_hours;
        } else {
          new_itv.to_ += 1_hours;
        }
        new_itv.to_ += ext;
      } else {
        if (can_extend_later(itv)) {
          new_itv.to_ += 1_hours;
        } else {
          new_itv.from_ -= 1_hours;
        }
        new_itv.from_ -= ext;
      }
    } else {
      if constexpr (SearchDir == direction::kForward) {
        if (q_.extend_interval_earlier_) {
          new_itv.from_ -= 1_hours + ext;
        }
      } else {
        if (q_.extend_interval_later_) {
          new_itv.to_ += 1_hours + ext;
        }
      }
    }

    clamp(new_itv);

    return new_itv;
  }

  interval<unixtime_t> extension(interval<unixtime_t> const& itv,
                                 std::uint32_t num_con_req,
                                 pareto_set<journey> const& journeys) const {
    if (num_con_req == 0 ||
        (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
      return itv;
    }

    auto new_itv = itv;

    auto const ontrip_ext = ontrip_estimate(itv, journeys);
    if (ontrip_ext != 0_minutes) {
      if constexpr (SearchDir == direction::kForward) {
        new_itv.to_ += ontrip_ext;
      } else {
        new_itv.from_ -= ontrip_ext;
      }
    } else {
      auto const ext = itv.size() * num_con_req;
      if (can_extend_both_dir(itv)) {
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

  bool can_extend_both_dir(interval<unixtime_t> const& itv) const {
    return can_extend_earlier(itv) && can_extend_later(itv);
  }

  void clamp(interval<unixtime_t>& itv) const {
    itv.from_ = tt_.external_interval().clamp(itv.from_);
    itv.to_ = tt_.external_interval().clamp(itv.to_);
    itv.from_ = data_type_max_interval_.clamp(itv.from_);
    itv.to_ = data_type_max_interval_.clamp(itv.to_);
  }

  i32_minutes ontrip_estimate(interval<unixtime_t> const& itv,
                              pareto_set<journey> const& journeys) const {
    if constexpr (SearchDir == direction::kForward) {
      if (!q_.extend_interval_later_) {
        return i32_minutes{0};
      }
    } else {
      if (!q_.extend_interval_earlier_) {
        return i32_minutes{0};
      }
    }

    auto ext = i32_minutes{std::numeric_limits<i32_minutes::rep>::max()};
    for (auto const& j : journeys) {
      if (itv.contains(j.start_time_)) {
        continue;
      }

      if constexpr (SearchDir == direction::kForward) {
        ext = std::min(j.dest_time_ - itv.to_, ext);
      } else {
        ext = std::min(itv.from_ - j.dest_time_, ext);
      }
    }

    return ext != i32_minutes{std::numeric_limits<i32_minutes::rep>::max()}
               ? ext
               : i32_minutes{0};
  }

  timetable const& tt_;
  query const& q_;
  interval<unixtime_t> data_type_max_interval_;
};

}  // namespace nigiri::routing