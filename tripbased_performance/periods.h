#pragma once

#include "nigiri/common/interval.h"
#include "nigiri/routing/tripbased/settings.h"
#include "date/date.h"

#ifndef AACHEN_TO
#define AACHEN_TO (2021_y / December / 11)
#endif
#ifndef VBB_TO
#define VBB_TO (2023_y / December / 9)
#endif
#ifndef GERMANY_TO
#define GERMANY_TO (2022_y / December / 10)
#endif

namespace nigiri::routing::tripbased::performance {

constexpr interval<std::chrono::sys_days> aachen_period() {
  using namespace date;
  constexpr auto const from = (2020_y / December / 14).operator sys_days();
  constexpr auto const to = (AACHEN_TO).operator sys_days();
  return {from, to};
}
constexpr interval<std::chrono::sys_days> vbb_period() {
  using namespace date;
  constexpr auto const from = (2022_y / December / 9).operator sys_days();
  constexpr auto const to = (VBB_TO).operator sys_days();
  return {from, to};
}
constexpr interval<std::chrono::sys_days> germany_period() {
  using namespace date;
  constexpr auto const from = (2021_y / November / 26).operator sys_days();
  constexpr auto const to = (GERMANY_TO).operator sys_days();
  return {from, to};
}
}  // namespace nigiri::routing::tripbased::performance