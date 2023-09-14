#include "nigiri/loader/dir.h"

#include <chrono>
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/preprocessing/preprocessor.h"
#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/routing/tripbased/transfer_set.h"
#include "nigiri/timetable.h"
#include "./paths.h"
#include "./periods.h"
#include "utl/progress_tracker.h"

using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::performance;

int main() {
  auto bars = utl::global_progress_bars{false};
  auto progress_tracker = utl::activate_progress_tracker("germany");

#ifdef ONLY_LOAD_TT
  using namespace std::chrono;
  auto const start_time = steady_clock::now();
#endif

  // init timetable
  timetable tt;
  tt.date_range_ = germany_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, loader::hrd::hrd_5_20_26, germany_dir, tt);
  finalize(tt);

#ifdef ONLY_LOAD_TT
  auto const stop_time = steady_clock::now();
  auto const time =
      std::chrono::duration<double, std::ratio<1>>(stop_time - start_time);
  print_tt_stats(tt, time);
#else
  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);
#endif
}