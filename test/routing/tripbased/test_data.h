#pragma once

#include "date/date.h"

#include "nigiri/loader/dir.h"
#include "nigiri/common/interval.h"

namespace nigiri::routing::tripbased::test {

// date range
constexpr interval<std::chrono::sys_days> gtfs_full_period() {
  using namespace date;
  constexpr auto const from =
      (2021_y / March / 1).operator sys_days();  // start, included
  constexpr auto const to =
      (2021_y / March / 8).operator sys_days();  // end, excluded
  return {from, to};
}

// test cases
nigiri::loader::mem_dir no_transfer_files();
nigiri::loader::mem_dir same_day_transfer_files();
nigiri::loader::mem_dir long_transfer_files();
nigiri::loader::mem_dir weekday_transfer_files();
nigiri::loader::mem_dir daily_transfer_files();
nigiri::loader::mem_dir earlier_stop_transfer_files();
nigiri::loader::mem_dir earlier_transport_transfer_files();
nigiri::loader::mem_dir uturn_transfer_files();
nigiri::loader::mem_dir unnecessary0_transfer_files();
nigiri::loader::mem_dir unnecessary1_transfer_files();
nigiri::loader::mem_dir enqueue_files();
nigiri::loader::mem_dir footpath_files();
nigiri::loader::mem_dir early_train_files();
nigiri::loader::mem_dir min_walk_files();
nigiri::loader::mem_dir transfer_class_files();

}  // namespace nigiri::routing::tripbased::test
