#pragma once

#include "nigiri/common/interval.h"
#include "date/date.h"

namespace nigiri::routing::tripbased::performance {

constexpr interval<std::chrono::sys_days> aachen_period() {
  using namespace date;
  constexpr auto const from = (2021_y / March / 01).operator sys_days();
  constexpr auto const to = (2021_y / March / 07).operator sys_days();
  return {from, to};
}

constexpr interval<std::chrono::sys_days> berlin_period() {
  using namespace date;
  constexpr auto const from = (2023_y / June / 15).operator sys_days();
  constexpr auto const to = (2023_y / December / 9).operator sys_days();
  return {from, to};
}

}  // namespace nigiri::routing::tripbased::performance