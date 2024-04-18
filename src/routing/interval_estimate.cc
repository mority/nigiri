#include "nigiri/routing/interval_estimate.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/query.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

constexpr auto const step = i32_minutes{60};
constexpr auto const half_step = step / 2;

template <direction SearchDir>
interval<unixtime_t> interval_estimator<SearchDir>::initial(
    interval<unixtime_t> const& itv) const {
  auto new_itv = itv;

  if (q_.min_connection_count_ == 0 ||
      (!q_.extend_interval_earlier_ && !q_.extend_interval_later_)) {
    return new_itv;
  }

  while (num_start_events(new_itv) < q_.min_connection_count_) {
    
    // check if further extension is possible
    if (!can_extend(new_itv)) {
      break;
    }

    // extend interval into allowed directions
    if (can_extend_both_dir(new_itv)) {
      new_itv.from_ -= half_step;
      new_itv.to_ += half_step;
    } else {
      if (q_.extend_interval_earlier_) {
        new_itv.from_ -= step;
      }
      if (q_.extend_interval_later_) {
        new_itv.to_ += step;
      }
    }

    // clamp to timetable
    new_itv.from_ = tt_.external_interval().clamp(new_itv.from_);
    new_itv.to_ = tt_.external_interval().clamp(new_itv.to_);
  }

  return new_itv;
}

template <direction SearchDir>
interval<unixtime_t> interval_estimator<SearchDir>::extension(
    interval<unixtime_t> const& itv,
    pareto_set<journey> const& results,
    std::uint32_t const num_con_req) const {
  auto new_itv = itv;
  if (!can_extend(new_itv)) {
    return new_itv;
  }

  // estimate extension amount
  auto ext = new_itv.size();
  if (results.size() > 0) {
    ext = ext_from_journeys(results, num_con_req);
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

template <direction SearchDir>
bool interval_estimator<SearchDir>::can_extend(
    interval<unixtime_t> const& itv) const {
  return (q_.extend_interval_earlier_ &&
          tt_.external_interval().from_ < itv.from_) ||
         (q_.extend_interval_later_ && itv.to_ < tt_.external_interval().to_);
}

template <direction SearchDir>
bool interval_estimator<SearchDir>::can_extend_both_dir(
    interval<unixtime_t> const& itv) const {
  return q_.extend_interval_earlier_ &&
         itv.from_ != tt_.external_interval().from_ &&
         q_.extend_interval_later_ && itv.to_ != tt_.external_interval().to_;
}

template <direction SearchDir>
std::uint32_t interval_estimator<SearchDir>::num_events(
    interval<unixtime_t> const& itv,
    location_idx_t const loc,
    std::vector<route_idx_t> const& routes) const {
  auto acc = 0U;
  for (auto const& route : routes) {
    auto const stop_seq = tt_.route_location_seq_[route];
    auto const stop_idx_start =
        SearchDir == direction::kForward ? stop_idx_t{0U} : stop_idx_t{1U};
    auto const stop_idx_end =
        SearchDir == direction::kForward
            ? static_cast<stop_idx_t>(stop_seq.size() - 1U)
            : static_cast<stop_idx_t>(stop_seq.size());
    for (auto stop_idx = stop_idx_start; stop_idx != stop_idx_end; ++stop_idx) {
      auto const s = stop{stop_seq[stop_idx]};
      if (s.location_idx() == loc) {
        auto const can_enter_exit =
            SearchDir == direction::kForward ? s.in_allowed() : s.out_allowed();
        if (can_enter_exit) {
          for (auto const transport_idx : tt_.route_transport_ranges_[route]) {
            auto const& bf =
                tt_.bitfields_[tt_.transport_traffic_days_[transport_idx]];
            for (auto day = 0U; day != bf.size(); ++day) {
              auto const et = SearchDir == direction::kForward
                                  ? event_type::kDep
                                  : event_type::kArr;
              if (bf.test(day) && itv.contains(tt_.event_time(
                                      transport{transport_idx, day_idx_t{day}},
                                      stop_idx, et))) {
                ++acc;
              }
            }
          }
        }
      }
    }
  }
  return acc / routes.size();
}

template <direction SearchDir>
std::uint32_t interval_estimator<SearchDir>::num_start_events(
    interval<unixtime_t> const& itv) const {

  auto const acc_fun = [&](auto const& acc, offset const& o) {
    return acc + num_events(itv, o.target(),
                            {begin(tt_.location_routes_[o.target()]),
                             end(tt_.location_routes_[o.target()])});
  };

  // auto const& offsets =
  //     SearchDir == direction::kForward ? q_.start_ : q_.destination_;
  auto const& offsets = q_.start_;
  return std::accumulate(begin(offsets), end(offsets), 0U, acc_fun);
}

template <direction SearchDir>
i32_minutes interval_estimator<SearchDir>::ext_from_journeys(
    pareto_set<journey> const& results, std::uint32_t const num_con_req) const {
  auto ext = i32_minutes{0U};
  for (auto const& j : results) {
    for (auto const& l : j.legs_) {
      if (!holds_alternative<journey::run_enter_exit>(l.uses_)) {
        continue;
      }

      auto const loc = SearchDir == direction::kForward ? l.from_ : l.to_;
      auto itv_at_loc = SearchDir == direction::kForward
                            ? interval<unixtime_t>{l.dep_time_ - half_step,
                                                   l.dep_time_ + half_step}
                            : interval<unixtime_t>{l.arr_time_ - half_step,
                                                   l.arr_time_ + half_step};
      auto const route = std::vector<route_idx_t>{
          tt_.transport_route_[get<journey::run_enter_exit>(l.uses_)
                                   .r_.t_.t_idx_]};
      while (num_events(itv_at_loc, loc, route) < num_con_req) {
        // can extend?
        if (!can_extend(itv_at_loc)) {
          break;
        }

        // extend
        if (can_extend_both_dir(itv_at_loc)) {
          itv_at_loc.from_ -= half_step;
          itv_at_loc.to_ += half_step;
        } else {
          if (q_.extend_interval_earlier_) {
            itv_at_loc.from_ -= step;
          }
          if (q_.extend_interval_later_) {
            itv_at_loc.to_ += step;
          }
        }

        // clamp
        itv_at_loc.from_ = tt_.external_interval().clamp(itv_at_loc.from_);
        itv_at_loc.to_ = tt_.external_interval().clamp(itv_at_loc.to_);
      }

      ext = std::max(ext, itv_at_loc.size());
    }
  }
  return ext;
}

template struct interval_estimator<direction::kForward>;
template struct interval_estimator<direction::kBackward>;
}  // namespace nigiri::routing