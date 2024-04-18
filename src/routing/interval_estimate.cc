#include "nigiri/routing/interval_estimate.h"

#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

constexpr auto const step = i32_minutes{60};
constexpr auto const half_step = step / 2;

template <direction SearchDir>
void interval_estimator<SearchDir>::initial_estimate(
    interval<unixtime_t>& itv) const {
  estimate(itv, q_.min_connection_count_);
}

template <direction SearchDir>
void interval_estimator<SearchDir>::estimate(interval<unixtime_t>& itv,
                                             std::uint32_t num_con_req) const {
  if (num_con_req == 0 ||
      (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
    return;
  }

  while (events_in_itv(itv) < num_con_req) {
    // check if further extension is possible
    if ((itv.from_ == tt_.external_interval().from_ ||
         !q_.extend_interval_earlier_) &&
        (itv.to_ == tt_.external_interval().to_ ||
         !q_.extend_interval_later_)) {
      break;
    }

    // extend interval into allowed directions
    if (q_.extend_interval_earlier_ &&
        itv.from_ != tt_.external_interval().from_ &&
        q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_) {
      itv.from_ -= half_step;
      itv.to_ += half_step;
    } else {
      if (q_.extend_interval_earlier_) {
        itv.from_ -= step;
      }
      if (q_.extend_interval_later_) {
        itv.to_ += step;
      }
    }

    // clamp to timetable
    itv.from_ = tt_.external_interval().clamp(itv.from_);
    itv.to_ = tt_.external_interval().clamp(itv.to_);
  }
}

template <direction SearchDir>
std::uint32_t interval_estimator<SearchDir>::events_in_itv(
    interval<unixtime_t> const& itv) const {

  auto const events_at_loc = [&](auto acc, offset const& o) {
    for (auto const& route : tt_.location_routes_[o.target_]) {
      auto const stop_seq = tt_.route_location_seq_[route];
      for (auto i = 0U; i != stop_seq.size(); ++i) {
        auto const s = stop{stop_seq[i]};
        if (s.location_idx() == o.target_) {
          auto const can_enter_exit = SearchDir == direction::kForward
                                          ? s.in_allowed()
                                          : s.out_allowed();
          if (can_enter_exit) {
            for (auto const transport_idx :
                 tt_.route_transport_ranges_[route]) {
              auto const& bf =
                  tt_.bitfields_[tt_.transport_traffic_days_[transport_idx]];
              for (auto day = 0U; i != bf.size(); ++day) {
                auto const et = SearchDir == direction::kForward
                                    ? event_type::kDep
                                    : event_type::kArr;
                if (bf.test(day) &&
                    itv.contains(tt_.event_time(
                        transport{transport_idx, day_idx_t{day}}, i, et))) {
                  ++acc;
                }
              }
            }
          }
        }
      }
    }
    return acc;
  };

  auto const& offsets =
      SearchDir == direction::kForward ? q_.start_ : q_.destination_;
  return std::accumulate(begin(offsets), end(offsets), 0U, events_at_loc);
}

}  // namespace nigiri::routing