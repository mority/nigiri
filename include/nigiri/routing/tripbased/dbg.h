#pragma once

#include "nigiri/timetable.h"
#include "nigiri/types.h"
#include <format>

#define TBDL std::cout << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

namespace nigiri::routing::tripbased {

static inline std::string dhhmm(nigiri::duration_t const& d) {
  nigiri::duration_t rem = d;
  unsigned days = 0;
  while (rem >= nigiri::duration_t{1440}) {
    rem -= nigiri::duration_t{1440};
    ++days;
  }
  unsigned hours = 0;
  while (rem >= nigiri::duration_t{60}) {
    rem -= nigiri::duration_t{60};
    ++hours;
  }
  std::string result = std::to_string(days) + "d" +
                       fmt::format("{:0>2}", hours) +
                       fmt::format("{:0>2}", rem.count()) + "h";
  return result;
}

static inline nigiri::duration_t unix_tt(nigiri::timetable const& tt,
                                         nigiri::unixtime_t const& u) {
  auto const [day, time] = tt.day_idx_mam(u);
  return nigiri::duration_t{day.v_ * 1440} + time;
}

static inline std::string unix_dhhmm(timetable const& tt, unixtime_t const u) {
  return dhhmm(unix_tt(tt, u));
}

static inline std::string location_name(timetable const& tt,
                                        location_idx_t const l) {
  auto const parent = tt.locations_.get(l).parent_;
  if (parent.v_ < tt.locations_.names_.size()) {
    return std::string{tt.locations_.names_.at(parent).view()};
  } else {
    return std::string{tt.locations_.names_.at(l).view()};
  }
}

}  // namespace nigiri::routing::tripbased