#include "gtest/gtest.h"

#include "nigiri/routing/reachability.h"

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "../loader/hrd/hrd_timetable.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::test_data::hrd_timetable;
using namespace nigiri::routing;

TEST(routing, reachability_forward) {
  constexpr auto const src = source_idx_t{0U};

  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  auto state = reachability_state{};
  auto r = reachability<direction::kForward>{tt, state, all_clasz_allowed()};
  r.reset();
  r.add_dest(tt.locations_.location_id_to_idx_.at({"0000001", src}));
  r.execute(kMaxTransfers, 0);

  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000001", src}).v_],
            0);
  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000002", src}).v_],
            1);
  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000003", src}).v_],
            2);
}

TEST(routing, reachability_backward) {
  constexpr auto const src = source_idx_t{0U};

  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  auto state = reachability_state{};
  auto r = reachability<direction::kBackward>{tt, state, all_clasz_allowed()};
  r.reset();
  r.add_dest(tt.locations_.location_id_to_idx_.at({"0000003", src}));
  r.execute(kMaxTransfers, 0);

  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000003", src}).v_],
            0);
  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000002", src}).v_],
            1);
  EXPECT_EQ(state.transports_to_dest_
                [tt.locations_.location_id_to_idx_.at({"0000001", src}).v_],
            2);
}