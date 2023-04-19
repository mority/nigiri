#pragma once

#include "date/date.h"

#include "nigiri/loader/dir.h"
#include "nigiri/common/interval.h"

namespace nigiri::routing::tripbased::test {

// date range
constexpr interval<std::chrono::sys_days> gtfs_full_period() {
  using namespace date;
  constexpr auto const from = (2021_y / March / 01).operator sys_days();
  constexpr auto const to = (2021_y / March / 07).operator sys_days();
  return {from, to};
}

// test cases
nigiri::loader::mem_dir no_transfer_files();
nigiri::loader::mem_dir same_day_transfer_files();
nigiri::loader::mem_dir long_transfer_files();
nigiri::loader::mem_dir weekday_transfer_files();

}  // namespace nigiri::routing::tripbased::test
