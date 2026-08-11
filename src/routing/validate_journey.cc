#include "nigiri/routing/validate_journey.h"

#include "fmt/format.h"

#include "utl/enumerate.h"
#include "utl/overloaded.h"

#include "nigiri/routing/journey.h"
#include "nigiri/rt/frun.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

journey_validation_result validate_journey(timetable const& tt,
                                           rt_timetable const& rtt,
                                           journey const& j) {
  auto result = journey_validation_result{};
  auto passenger_time = std::optional<unixtime_t>{};

  for (auto const [i, leg] : utl::enumerate(j.legs_)) {
    auto const infeasible = [&](std::string&& reason) {
      result.feasible_ = false;
      result.broken_leg_idx_ = i;
      result.reason_ = std::move(reason);
      result.arrival_time_ = passenger_time.value_or(leg.dep_time_);
    };

    auto const ok = std::visit(
        utl::overloaded{
            [&](journey::run_enter_exit const& ree) {
              // Re-resolve against the CURRENT rtt, independent of whatever
              // realtime state (if any) the journey was reconstructed with.
              auto const rt_t = ree.r_.t_.is_valid()
                                    ? rtt.resolve_rt(ree.r_.t_)
                                    : ree.r_.rt_;
              auto const fr =
                  rt::frun{tt, &rtt, rt::run{ree.r_.t_, ree.r_.stop_range_, rt_t}};

              if (fr.is_cancelled()) {
                infeasible("trip cancelled");
                return false;
              }

              auto const board = fr[ree.stop_range_.from_];
              auto const alight = fr[static_cast<stop_idx_t>(
                  ree.stop_range_.to_ - 1U)];
              auto const actual_dep = board.time(event_type::kDep);
              auto const actual_arr = alight.time(event_type::kArr);

              // Loose (<=), matching raptor's own boarding comparison: ready
              // exactly when the transport departs still counts as caught.
              if (passenger_time.has_value() && *passenger_time > actual_dep) {
                infeasible(fmt::format(
                    "missed connection at location #{}: ready {}, transport "
                    "departs {}",
                    to_idx(board.get_location_idx()), *passenger_time,
                    actual_dep));
                return false;
              }

              passenger_time = actual_arr;
              return true;
            },
            [&](footpath const&) {
              // fixed duration, not realtime-adjustable in this codebase --
              // just carry the passenger forward.
              auto const duration = leg.arr_time_ - leg.dep_time_;
              passenger_time =
                  passenger_time.value_or(leg.dep_time_) + duration;
              return true;
            },
            [&](offset const&) {
              auto const duration = leg.arr_time_ - leg.dep_time_;
              passenger_time =
                  passenger_time.value_or(leg.dep_time_) + duration;
              return true;
            }},
        leg.uses_);

    if (!ok) {
      return result;
    }
  }

  result.feasible_ = true;
  result.arrival_time_ = passenger_time.value_or(j.dest_time_);
  return result;
}

}  // namespace nigiri::routing
