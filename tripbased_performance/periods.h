#pragma once

#include "nigiri/common/interval.h"
#include "date/date.h"

namespace nigiri::routing::tripbased::performance {

constexpr interval<std::chrono::sys_days> aachen_period() {
  using namespace date;
  constexpr auto const from = (2020_y / December / 14).operator sys_days();
  constexpr auto const to = (2021_y / December / 11).operator sys_days();
  return {from, to};
}
constexpr interval<std::chrono::sys_days> vbb_period() {
  using namespace date;
  constexpr auto const from = (2022_y / December / 9).operator sys_days();
  constexpr auto const to = (2023_y / December / 9).operator sys_days();
  return {from, to};
}

}  // namespace nigiri::routing::tripbased::performance