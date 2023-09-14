#pragma once

#include <sys/resource.h>
#include "nigiri/timetable.h"
#include "nigiri/types.h"
#include <format>

#define TBDL std::cout << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

namespace nigiri::routing::tripbased {

static inline std::string dhhmm(std::int32_t const& d) {
  auto rem = d;
  unsigned days = 0;
  while (rem >= 1440) {
    rem -= 1440;
    ++days;
  }
  unsigned hours = 0;
  while (rem >= 60) {
    rem -= 60;
    ++hours;
  }
  std::string result = std::to_string(days) + "d" +
                       fmt::format("{:0>2}", hours) +
                       fmt::format("{:0>2}", rem) + "h";
  return result;
}

static inline std::int32_t unix_tt(nigiri::timetable const& tt,
                                   nigiri::unixtime_t const& u) {
  auto const [day, time] = tt.day_idx_mam(u);
  return day.v_ * 1440 + time.count();
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

static inline std::string transfer_str(timetable const& tt,
                                       transport_idx_t transport_from,
                                       stop_idx_t stop_from,
                                       transport_idx_t transport_to,
                                       stop_idx_t stop_to) {
  auto const location_idx_from =
      stop{tt.route_location_seq_[tt.transport_route_[transport_from]]
                                 [stop_from]}
          .location_idx();
  auto const location_idx_to =
      stop{tt.route_location_seq_[tt.transport_route_[transport_to]][stop_to]}
          .location_idx();
  return "transfer( " + location_name(tt, location_idx_from) + " -> " +
         location_name(tt, location_idx_to) + " )";
}

static inline void print_tt_stats(
    timetable const& tt, std::chrono::duration<double, std::ratio<1>> time) {
  rusage r_usage;
  getrusage(RUSAGE_SELF, &r_usage);
  auto const peak_memory_usage = static_cast<double>(r_usage.ru_maxrss) / 1e6;
  std::stringstream ss;
  ss << "\n--- Timetable Stats ---\n"
     << "Days: " << tt.date_range_.size().count()
     << "\nStops: " << tt.n_locations() << "\nRoutes: " << tt.n_routes()
     << "\nTransports: " << tt.transport_route_.size()
     << "\nPeak memory usage: " << peak_memory_usage << " Gigabyte"
     << "\nLoading time: " << time.count()
     << " seconds\n-----------------------\n";
  std::cout << ss.str();
}

}  // namespace nigiri::routing::tripbased