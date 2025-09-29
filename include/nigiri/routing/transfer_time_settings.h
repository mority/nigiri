#pragma once

#include "nigiri/types.h"

namespace nigiri::routing {

struct transfer_time_settings {
  transfer_time_settings(duration_t min_transfer_time,
                         duration_t additional_time,
                         float factor)
      : factor_{std::max(factor, 1.0F)},
        min_transfer_time_{std::max(min_transfer_time, 0_minutes)},
        additional_time_{std::max(additional_time, 0_minutes)},
        default_{factor == 1.0F  //
                 && min_transfer_time == 0_minutes  //
                 && additional_time == 0_minutes} {}

  bool operator==(transfer_time_settings const& o) const {
    return default_ == o.default_ ||
           std::tie(min_transfer_time_, additional_time_, factor_) ==
               std::tie(o.min_transfer_time_, additional_time_, o.factor_);
  }

  float factor_;
  duration_t min_transfer_time_;
  duration_t additional_time_;
  bool default_;
};

template <typename T>
inline T adjusted_transfer_time(transfer_time_settings const& settings,
                                T const duration) {
  if (settings.default_) {
    return duration;
  } else {
    return static_cast<T>(settings.additional_time_.count()) +
           std::max(
               static_cast<T>(settings.min_transfer_time_.count()),
               static_cast<T>(static_cast<float>(duration) * settings.factor_));
  }
}

template <typename Rep>
inline std::chrono::duration<Rep, std::ratio<60>> adjusted_transfer_time(
    transfer_time_settings const& settings,
    std::chrono::duration<Rep, std::ratio<60>> const duration) {
  if (settings.default_) {
    return duration;
  } else {
    return std::chrono::duration<Rep, std::ratio<60>>{
        static_cast<Rep>(settings.additional_time_.count()) +
        std::max(static_cast<Rep>(settings.min_transfer_time_.count()),
                 static_cast<Rep>(static_cast<float>(duration.count()) *
                                  settings.factor_))};
  }
}

}  // namespace nigiri::routing
