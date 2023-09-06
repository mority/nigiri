#include "nigiri/loader/dir.h"

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

#include <sys/resource.h>

using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::performance;

int main() {
  auto bars = utl::global_progress_bars{false};
  auto progress_tracker = utl::activate_progress_tracker("germany");

  // init timetable
  timetable tt;
  tt.date_range_ = germany_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, loader::hrd::hrd_5_20_26, germany_dir, tt);
  finalize(tt);

#ifdef ONLY_LOAD_TT
  print_tt_stats(tt);
#else
  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);
#endif

  rusage r_usage;
  getrusage(RUSAGE_SELF, &r_usage);
  std::cout << "Peak memory usage = "
            << static_cast<double>(r_usage.ru_maxrss) / 1e6 << " Gigabyte\n";
}