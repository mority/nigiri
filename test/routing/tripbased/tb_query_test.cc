#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/tripbased/query/query_engine.h"

#include "../../loader/hrd/hrd_timetable.h"

#include "./test_data.h"

#include <chrono>
#include "./tb_query_test.h"

using namespace std::chrono;
using namespace nigiri;
using namespace nigiri::test;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;
using namespace nigiri::test_data::hrd_timetable;

#if !defined(TB_MIN_WALK) && !defined(TB_TRANSFER_CLASS)
TEST(tb_query, enqueue) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, enqueue_files(), tt);
  finalize(tt);

  // init preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts, 10);

  // init query
  day_idx_t const base{5U};
  query_state state{tt, ts};
  bitvec is_dest;
  std::vector<std::uint16_t> dist_to_dest;
  std::vector<std::uint16_t> lb;
  query_engine tbq{tt,           nullptr,      state,        is_dest,
                   std::nullopt, dist_to_dest, std::nullopt, lb,
                   std::nullopt, base,         std::nullopt, std::nullopt,
                   std::nullopt, std::nullopt};

  transport_idx_t const tpi0{0U};
  auto const si3{3U};
  day_idx_t const day_idx5{5U};
  state.q_n_.enqueue(day_idx5.v_, tpi0, si3, 0, TRANSFERRED_FROM_NULL);
  EXPECT_EQ(0, tbq.get_state().q_n_.start_[0]);
  EXPECT_EQ(1, tbq.get_state().q_n_.end_[0]);
  ASSERT_EQ(1, state.q_n_.size());
  EXPECT_EQ(tpi0, state.q_n_[0].get_transport_idx());
  EXPECT_EQ(si3, state.q_n_[0].stop_idx_start_);
  EXPECT_EQ(5, state.q_n_[0].stop_idx_end_);
  EXPECT_EQ(day_idx5, state.q_n_[0].get_transport_day(base));

  day_idx_t const day_idx6{6U};
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx5.v_, tpi0), 0));
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx6.v_, tpi0), 0));
  transport_idx_t const tpi1{1U};
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx5.v_, tpi1), 0));
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx6.v_, tpi1), 0));

  auto const si2{2U};
  state.q_n_.enqueue(day_idx5.v_, tpi1, si2, 1, TRANSFERRED_FROM_NULL);
  EXPECT_EQ(0, tbq.get_state().q_n_.start_[0]);
  EXPECT_EQ(1, tbq.get_state().q_n_.end_[0]);
  EXPECT_EQ(1, tbq.get_state().q_n_.start_[1]);
  EXPECT_EQ(2, tbq.get_state().q_n_.end_[1]);
  ASSERT_EQ(2, state.q_n_.size());
  EXPECT_EQ(tpi0, state.q_n_[0].get_transport_idx());
  EXPECT_EQ(si3, state.q_n_[0].stop_idx_start_);
  EXPECT_EQ(5, state.q_n_[0].stop_idx_end_);
  EXPECT_EQ(day_idx5, state.q_n_[0].get_transport_day(base));
  EXPECT_EQ(tpi1, state.q_n_[1].get_transport_idx());
  EXPECT_EQ(si2, state.q_n_[1].stop_idx_start_);
  EXPECT_EQ(si3, state.q_n_[1].stop_idx_end_);
  EXPECT_EQ(day_idx5, state.q_n_[1].get_transport_day(base));
  day_idx_t const day_idx2{2U};
  EXPECT_EQ(tt.route_location_seq_[tt.transport_route_[tpi0]].size() - 1,
            tbq.get_state().r_.query(
                embed_day_offset(base.v_, day_idx2.v_, tpi0), 0));
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx5.v_, tpi0), 0));
  EXPECT_EQ(si2, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx5.v_, tpi1), 1));
  EXPECT_EQ(si3, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx5.v_, tpi0), 1));
  EXPECT_EQ(si2, tbq.get_state().r_.query(
                     embed_day_offset(base.v_, day_idx6.v_, tpi0), 1));
}
#endif

constexpr auto const same_day_transfer_journeys = R"(
[2021-02-28 23:00, 2021-03-01 13:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S2, S2) [2021-03-01 13:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 06:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0_MON, src=0}]
   1: S1      S1.............................................. a: 01.03 06:00 [01.03 06:00]
leg 1: (S1, S1) [2021-03-01 06:00] -> (S1, S1) [2021-03-01 06:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 12:00] -> (S2, S2) [2021-03-01 13:00]
   0: S1      S1..............................................                               d: 01.03 12:00 [01.03 12:00]  [{name=R1, day=2021-03-01, id=R1_MON, src=0}]
   1: S2      S2.............................................. a: 01.03 13:00 [01.03 13:00]
leg 3: (S2, S2) [2021-03-01 13:00] -> (S2, S2) [2021-03-01 13:00]
  FOOTPATH (duration=0)


)";

TEST(earliest_arrival_query, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, same_day_transfer_files(),
                 tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "S0", "S2", unixtime_t{sys_days{February / 28 / 2021} + 23h});

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{same_day_transfer_journeys}, ss.str());
}

constexpr auto const early_train_journeys = R"(
[2021-03-04 05:00, 2021-03-04 09:00]
TRANSFERS: 1
     FROM: (S1, S1) [2021-03-04 06:00]
       TO: (S3, S3) [2021-03-04 09:00]
leg 0: (S1, S1) [2021-03-04 06:00] -> (S2, S2) [2021-03-04 07:00]
   0: S1      S1..............................................                               d: 04.03 06:00 [04.03 06:00]  [{name=R1, day=2021-03-04, id=R1_THU, src=0}]
   1: S2      S2.............................................. a: 04.03 07:00 [04.03 07:00]
leg 1: (S2, S2) [2021-03-04 07:00] -> (S2, S2) [2021-03-04 07:02]
  FOOTPATH (duration=2)
leg 2: (S2, S2) [2021-03-04 08:00] -> (S3, S3) [2021-03-04 09:00]
   1: S2      S2..............................................                               d: 04.03 08:00 [04.03 08:00]  [{name=R0, day=2021-03-01, id=R0_MON, src=0}]
   2: S3      S3.............................................. a: 04.03 09:00 [04.03 09:00]
leg 3: (S3, S3) [2021-03-04 09:00] -> (S3, S3) [2021-03-04 09:00]
  FOOTPATH (duration=0)


)";

TEST(earliest_arrival_query, early_train) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, early_train_files(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "S1", "S3", unixtime_t{sys_days{March / 04 / 2021} + 5h});

  EXPECT_EQ(1, results.size());

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  std::cout << "---------------- ACTUAL --------------------\n"
            << ss.str() << "-----------------------------------------------\n";

  EXPECT_EQ(std::string_view{early_train_journeys}, ss.str());
}

constexpr auto const earlier_stop_transfer_journeys = R"(
[2021-02-28 23:00, 2021-03-01 06:00]
TRANSFERS: 1
     FROM: (S3, S3) [2021-03-01 03:00]
       TO: (S2, S2) [2021-03-01 06:00]
leg 0: (S3, S3) [2021-03-01 03:00] -> (S1, S1) [2021-03-01 04:00]
   3: S3      S3..............................................                               d: 01.03 03:00 [01.03 03:00]  [{name=R0, day=2021-03-01, id=R0_MON0, src=0}]
   4: S1      S1.............................................. a: 01.03 04:00 [01.03 04:00]
leg 1: (S1, S1) [2021-03-01 04:00] -> (S1, S1) [2021-03-01 04:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 05:00] -> (S2, S2) [2021-03-01 06:00]
   1: S1      S1..............................................                               d: 01.03 05:00 [01.03 05:00]  [{name=R0, day=2021-03-01, id=R0_MON1, src=0}]
   2: S2      S2.............................................. a: 01.03 06:00 [01.03 06:00]
leg 3: (S2, S2) [2021-03-01 06:00] -> (S2, S2) [2021-03-01 06:00]
  FOOTPATH (duration=0)


)";

TEST(earliest_arrival_query, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 earlier_stop_transfer_files(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "S3", "S2", unixtime_t{sys_days{February / 28 / 2021} + 23h});

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  std::cout << ss.str();

  EXPECT_EQ(std::string_view{earlier_stop_transfer_journeys}, ss.str());
}

TEST(earliest_arrival_query, no_journey_possible) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 earlier_stop_transfer_files(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "S4", "S0", unixtime_t{sys_days{February / 28 / 2021} + 23h});

  EXPECT_EQ(0, results.size());
}

#ifdef TB_MIN_WALK
constexpr auto const min_walk_journeys = R"(
[2021-02-28 23:00, 2021-03-01 03:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S5, S5) [2021-03-01 03:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 01:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0, src=0}]
   1: S1      S1.............................................. a: 01.03 01:00 [01.03 01:00]
leg 1: (S1, S1) [2021-03-01 01:00] -> (S2, S2) [2021-03-01 01:30]
  FOOTPATH (duration=30)
leg 2: (S2, S2) [2021-03-01 02:00] -> (S5, S5) [2021-03-01 03:00]
   0: S2      S2..............................................                               d: 01.03 02:00 [01.03 02:00]  [{name=R1, day=2021-03-01, id=R1, src=0}]
   1: S5      S5.............................................. a: 01.03 03:00 [01.03 03:00]
leg 3: (S5, S5) [2021-03-01 03:00] -> (S5, S5) [2021-03-01 03:00]
  FOOTPATH (duration=0)


[2021-02-28 23:00, 2021-03-01 04:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S5, S5) [2021-03-01 04:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 01:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0, src=0}]
   1: S1      S1.............................................. a: 01.03 01:00 [01.03 01:00]
leg 1: (S1, S1) [2021-03-01 01:00] -> (S3, S3) [2021-03-01 01:20]
  FOOTPATH (duration=20)
leg 2: (S3, S3) [2021-03-01 02:00] -> (S5, S5) [2021-03-01 04:00]
   0: S3      S3..............................................                               d: 01.03 02:00 [01.03 02:00]  [{name=R2, day=2021-03-01, id=R2, src=0}]
   1: S5      S5.............................................. a: 01.03 04:00 [01.03 04:00]
leg 3: (S5, S5) [2021-03-01 04:00] -> (S5, S5) [2021-03-01 04:00]
  FOOTPATH (duration=0)


[2021-02-28 23:00, 2021-03-01 05:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S5, S5) [2021-03-01 05:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 01:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0, src=0}]
   1: S1      S1.............................................. a: 01.03 01:00 [01.03 01:00]
leg 1: (S1, S1) [2021-03-01 01:00] -> (S4, S4) [2021-03-01 01:10]
  FOOTPATH (duration=10)
leg 2: (S4, S4) [2021-03-01 02:00] -> (S5, S5) [2021-03-01 05:00]
   0: S4      S4..............................................                               d: 01.03 02:00 [01.03 02:00]  [{name=R3, day=2021-03-01, id=R3, src=0}]
   1: S5      S5.............................................. a: 01.03 05:00 [01.03 05:00]
leg 3: (S5, S5) [2021-03-01 05:00] -> (S5, S5) [2021-03-01 05:00]
  FOOTPATH (duration=0)


)";
#else
constexpr auto const min_walk_journeys = R"(
[2021-02-28 23:00, 2021-03-01 03:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S5, S5) [2021-03-01 03:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 01:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0, src=0}]
   1: S1      S1.............................................. a: 01.03 01:00 [01.03 01:00]
leg 1: (S1, S1) [2021-03-01 01:00] -> (S2, S2) [2021-03-01 01:30]
  FOOTPATH (duration=30)
leg 2: (S2, S2) [2021-03-01 02:00] -> (S5, S5) [2021-03-01 03:00]
   0: S2      S2..............................................                               d: 01.03 02:00 [01.03 02:00]  [{name=R1, day=2021-03-01, id=R1, src=0}]
   1: S5      S5.............................................. a: 01.03 03:00 [01.03 03:00]
leg 3: (S5, S5) [2021-03-01 03:00] -> (S5, S5) [2021-03-01 03:00]
  FOOTPATH (duration=0)


)";
#endif

TEST(earliest_arrival_query, min_walk) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, min_walk_files(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "S0", "S5", unixtime_t{sys_days{February / 28 / 2021} + 23h});

#ifdef TB_MIN_WALK
  EXPECT_EQ(3, results.size());
#else
  EXPECT_EQ(1, results.size());
#endif

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{min_walk_journeys}, ss.str());
}

#ifdef TB_TRANSFER_CLASS
constexpr auto const transfer_class_journeys = R"(
[2021-02-28 23:00, 2021-03-01 02:04]
TRANSFERS: 1
     FROM: (A, A) [2021-03-01 00:00]
       TO: (C, C) [2021-03-01 02:04]
leg 0: (A, A) [2021-03-01 00:00] -> (B, B) [2021-03-01 01:00]
   0: A       A...............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R1, day=2021-03-01, id=R1_0, src=0}]
   1: B       B............................................... a: 01.03 01:00 [01.03 01:00]
leg 1: (B, B) [2021-03-01 01:00] -> (B, B) [2021-03-01 01:02]
  FOOTPATH (duration=2)
leg 2: (B, B) [2021-03-01 01:04] -> (C, C) [2021-03-01 02:04]
   0: B       B...............................................                               d: 01.03 01:04 [01.03 01:04]  [{name=R2, day=2021-03-01, id=R2_0, src=0}]
   1: C       C............................................... a: 01.03 02:04 [01.03 02:04]
leg 3: (C, C) [2021-03-01 02:04] -> (C, C) [2021-03-01 02:04]
  FOOTPATH (duration=0)


[2021-02-28 23:00, 2021-03-01 02:09]
TRANSFERS: 1
     FROM: (A, A) [2021-03-01 00:00]
       TO: (C, C) [2021-03-01 02:09]
leg 0: (A, A) [2021-03-01 00:00] -> (B, B) [2021-03-01 01:00]
   0: A       A...............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R1, day=2021-03-01, id=R1_0, src=0}]
   1: B       B............................................... a: 01.03 01:00 [01.03 01:00]
leg 1: (B, B) [2021-03-01 01:00] -> (B, B) [2021-03-01 01:02]
  FOOTPATH (duration=2)
leg 2: (B, B) [2021-03-01 01:09] -> (C, C) [2021-03-01 02:09]
   0: B       B...............................................                               d: 01.03 01:09 [01.03 01:09]  [{name=R2, day=2021-03-01, id=R2_1, src=0}]
   1: C       C............................................... a: 01.03 02:09 [01.03 02:09]
leg 3: (C, C) [2021-03-01 02:09] -> (C, C) [2021-03-01 02:09]
  FOOTPATH (duration=0)


[2021-02-28 23:00, 2021-03-01 02:18]
TRANSFERS: 1
     FROM: (A, A) [2021-03-01 00:00]
       TO: (C, C) [2021-03-01 02:18]
leg 0: (A, A) [2021-03-01 00:00] -> (B, B) [2021-03-01 01:00]
   0: A       A...............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R1, day=2021-03-01, id=R1_0, src=0}]
   1: B       B............................................... a: 01.03 01:00 [01.03 01:00]
leg 1: (B, B) [2021-03-01 01:00] -> (B, B) [2021-03-01 01:02]
  FOOTPATH (duration=2)
leg 2: (B, B) [2021-03-01 01:18] -> (C, C) [2021-03-01 02:18]
   0: B       B...............................................                               d: 01.03 01:18 [01.03 01:18]  [{name=R2, day=2021-03-01, id=R2_2, src=0}]
   1: C       C............................................... a: 01.03 02:18 [01.03 02:18]
leg 3: (C, C) [2021-03-01 02:18] -> (C, C) [2021-03-01 02:18]
  FOOTPATH (duration=0)


)";
#else
constexpr auto const transfer_class_journeys = R"(
[2021-02-28 23:00, 2021-03-01 02:04]
TRANSFERS: 1
     FROM: (A, A) [2021-03-01 00:00]
       TO: (C, C) [2021-03-01 02:04]
leg 0: (A, A) [2021-03-01 00:00] -> (B, B) [2021-03-01 01:00]
   0: A       A...............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R1, day=2021-03-01, id=R1_0, src=0}]
   1: B       B............................................... a: 01.03 01:00 [01.03 01:00]
leg 1: (B, B) [2021-03-01 01:00] -> (B, B) [2021-03-01 01:02]
  FOOTPATH (duration=2)
leg 2: (B, B) [2021-03-01 01:04] -> (C, C) [2021-03-01 02:04]
   0: B       B...............................................                               d: 01.03 01:04 [01.03 01:04]  [{name=R2, day=2021-03-01, id=R2_0, src=0}]
   1: C       C............................................... a: 01.03 02:04 [01.03 02:04]
leg 3: (C, C) [2021-03-01 02:04] -> (C, C) [2021-03-01 02:04]
  FOOTPATH (duration=0)


)";
#endif

TEST(earliest_arrival_query, transfer_class) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, transfer_class_files(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "A", "C", unixtime_t{sys_days{February / 28 / 2021} + 23h});

#ifdef TB_TRANSFER_CLASS
  EXPECT_EQ(3, results.size());
#else
  EXPECT_EQ(1, results.size());
#endif

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{transfer_class_journeys}, ss.str());
}

TEST(earliest_arrival_query, transfer_class2) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  register_special_stations(tt);
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, transfer_class_2_files(),
                 tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "A", "C", unixtime_t{sys_days{February / 28 / 2021} + 23h});

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  std::cout << "--------------------- ACTUAL -----------------------------"
            << ss.str()
            << "\n------------------------------------------------\n";

  // EXPECT_EQ(std::string_view{transfer_class_journeys}, ss.str());
}

#ifdef TB_TRANSFER_CLASS
constexpr auto const abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)


[2020-03-30 05:00, 2020-03-30 07:30]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:30]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:30] -> (C, 0000003) [2020-03-30 07:30]
   0: 0000002 B...............................................                               d: 30.03 06:30 [30.03 08:30]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/390/0000003/450/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:30 [30.03 09:30]
leg 3: (C, 0000003) [2020-03-30 07:30] -> (C, 0000003) [2020-03-30 07:30]
  FOOTPATH (duration=0)


)";
#else
constexpr auto const abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)


)";
#endif

TEST(earliest_arrival_query, files_abc) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  register_special_stations(tt);
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  auto const results = tripbased_search(
      tt, "0000001", "0000003", unixtime_t{sys_days{March / 30 / 2020} + 5h});

#ifdef TB_TRANSFER_CLASS
  EXPECT_EQ(2, results.size());
#else
  EXPECT_EQ(1, results.size());
#endif

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{abc_journeys}, ss.str());
}

#ifdef TB_TRANSFER_CLASS
constexpr auto const profile_abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)


[2020-03-30 05:00, 2020-03-30 07:30]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:30]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:30] -> (C, 0000003) [2020-03-30 07:30]
   0: 0000002 B...............................................                               d: 30.03 06:30 [30.03 08:30]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/390/0000003/450/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:30 [30.03 09:30]
leg 3: (C, 0000003) [2020-03-30 07:30] -> (C, 0000003) [2020-03-30 07:30]
  FOOTPATH (duration=0)


[2020-03-30 05:30, 2020-03-30 07:45]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:30]
       TO: (C, 0000003) [2020-03-30 07:45]
leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 3: (C, 0000003) [2020-03-30 07:45] -> (C, 0000003) [2020-03-30 07:45]
  FOOTPATH (duration=0)


[2020-03-30 05:30, 2020-03-30 08:00]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:30]
       TO: (C, 0000003) [2020-03-30 08:00]
leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 07:00] -> (C, 0000003) [2020-03-30 08:00]
   0: 0000002 B...............................................                               d: 30.03 07:00 [30.03 09:00]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/420/0000003/480/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:00 [30.03 10:00]
leg 3: (C, 0000003) [2020-03-30 08:00] -> (C, 0000003) [2020-03-30 08:00]
  FOOTPATH (duration=0)


)";
#else
constexpr auto const profile_abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)


[2020-03-30 05:30, 2020-03-30 07:45]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:30]
       TO: (C, 0000003) [2020-03-30 07:45]
leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 3: (C, 0000003) [2020-03-30 07:45] -> (C, 0000003) [2020-03-30 07:45]
  FOOTPATH (duration=0)


)";
#endif

TEST(profile_query, files_abc) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  register_special_stations(tt);
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  auto const results =
      tripbased_search(tt, "0000001", "0000003",
                       interval{unixtime_t{sys_days{March / 30 / 2020}} + 5h,
                                unixtime_t{sys_days{March / 30 / 2020}} + 6h});

#ifdef TB_TRANSFER_CLASS
  EXPECT_EQ(4, results.size());
#else
  EXPECT_EQ(2, results.size());
#endif

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{profile_abc_journeys}, ss.str());
}

#ifdef TB_TRANSFER_CLASS
constexpr auto const intermodal_abc_journeys = R"(
[2020-03-30 05:20, 2020-03-30 08:00]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:20]
       TO: (END, END) [2020-03-30 08:00]
leg 0: (START, START) [2020-03-30 05:20] -> (A, 0000001) [2020-03-30 05:30]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 2: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 4: (C, 0000003) [2020-03-30 07:45] -> (END, END) [2020-03-30 08:00]
  MUMO (id=77, duration=15)


[2020-03-30 05:20, 2020-03-30 08:15]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:20]
       TO: (END, END) [2020-03-30 08:15]
leg 0: (START, START) [2020-03-30 05:20] -> (A, 0000001) [2020-03-30 05:30]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 2: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 07:00] -> (C, 0000003) [2020-03-30 08:00]
   0: 0000002 B...............................................                               d: 30.03 07:00 [30.03 09:00]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/420/0000003/480/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:00 [30.03 10:00]
leg 4: (C, 0000003) [2020-03-30 08:00] -> (END, END) [2020-03-30 08:15]
  MUMO (id=77, duration=15)


[2020-03-30 05:50, 2020-03-30 08:30]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:50]
       TO: (END, END) [2020-03-30 08:30]
leg 0: (START, START) [2020-03-30 05:50] -> (A, 0000001) [2020-03-30 06:00]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 07:00]
   0: 0000001 A...............................................                               d: 30.03 06:00 [30.03 08:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/360/0000002/420/, src=0}]
   1: 0000002 B............................................... a: 30.03 07:00 [30.03 09:00]
leg 2: (B, 0000002) [2020-03-30 07:00] -> (B, 0000002) [2020-03-30 07:02]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 08:15]
   0: 0000002 B...............................................                               d: 30.03 07:15 [30.03 09:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/435/0000003/495/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:15 [30.03 10:15]
leg 4: (C, 0000003) [2020-03-30 08:15] -> (END, END) [2020-03-30 08:30]
  MUMO (id=77, duration=15)


[2020-03-30 05:50, 2020-03-30 08:45]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:50]
       TO: (END, END) [2020-03-30 08:45]
leg 0: (START, START) [2020-03-30 05:50] -> (A, 0000001) [2020-03-30 06:00]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 07:00]
   0: 0000001 A...............................................                               d: 30.03 06:00 [30.03 08:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/360/0000002/420/, src=0}]
   1: 0000002 B............................................... a: 30.03 07:00 [30.03 09:00]
leg 2: (B, 0000002) [2020-03-30 07:00] -> (B, 0000002) [2020-03-30 07:02]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 07:30] -> (C, 0000003) [2020-03-30 08:30]
   0: 0000002 B...............................................                               d: 30.03 07:30 [30.03 09:30]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/450/0000003/510/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:30 [30.03 10:30]
leg 4: (C, 0000003) [2020-03-30 08:30] -> (END, END) [2020-03-30 08:45]
  MUMO (id=77, duration=15)


)";
#else
constexpr auto const intermodal_abc_journeys = R"(
[2020-03-30 05:20, 2020-03-30 08:00]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:20]
       TO: (END, END) [2020-03-30 08:00]
leg 0: (START, START) [2020-03-30 05:20] -> (A, 0000001) [2020-03-30 05:30]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 2: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 4: (C, 0000003) [2020-03-30 07:45] -> (END, END) [2020-03-30 08:00]
  MUMO (id=77, duration=15)


[2020-03-30 05:50, 2020-03-30 08:30]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:50]
       TO: (END, END) [2020-03-30 08:30]
leg 0: (START, START) [2020-03-30 05:50] -> (A, 0000001) [2020-03-30 06:00]
  MUMO (id=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 07:00]
   0: 0000001 A...............................................                               d: 30.03 06:00 [30.03 08:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/360/0000002/420/, src=0}]
   1: 0000002 B............................................... a: 30.03 07:00 [30.03 09:00]
leg 2: (B, 0000002) [2020-03-30 07:00] -> (B, 0000002) [2020-03-30 07:02]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 08:15]
   0: 0000002 B...............................................                               d: 30.03 07:15 [30.03 09:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/435/0000003/495/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:15 [30.03 10:15]
leg 4: (C, 0000003) [2020-03-30 08:15] -> (END, END) [2020-03-30 08:30]
  MUMO (id=77, duration=15)


)";
#endif

TEST(profile_query, intermodal) {
  constexpr auto const src = source_idx_t{0U};

  timetable tt;
  tt.date_range_ = full_period();
  register_special_stations(tt);
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  auto const results = tripbased_intermodal_search(
      tt,
      {{tt.locations_.location_id_to_idx_.at({.id_ = "0000001", .src_ = src}),
        10_minutes, 99U}},
      {{tt.locations_.location_id_to_idx_.at({.id_ = "0000003", .src_ = src}),
        15_minutes, 77U}},
      interval{unixtime_t{sys_days{March / 30 / 2020}} + 5_hours,
               unixtime_t{sys_days{March / 30 / 2020}} + 6_hours});

#ifdef TB_TRANSFER_CLASS
  EXPECT_EQ(4, results.size());
#else
  EXPECT_EQ(2, results.size());
#endif

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{intermodal_abc_journeys}, ss.str());
}