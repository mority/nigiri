#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/tripbased/tb_query.h"

#include "../../loader/hrd/hrd_timetable.h"

#include "./test_data.h"

using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;

TEST(tb_query, r_) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  // constants
  transport_idx_t const tpi{22U};
  day_idx_t const day0{0U};
  day_idx_t const day1{1U};
  day_idx_t const day2{2U};
  day_idx_t const day3{3U};

  // update empty
  tbq.r_update(tpi, 42, day2);
  EXPECT_EQ(42, tbq.r_query(tpi, day2));

  // update partial dominance
  tbq.r_update(tpi, 41, day3);
  EXPECT_EQ(42, tbq.r_query(tpi, day2));
  EXPECT_EQ(41, tbq.r_query(tpi, day3));
  EXPECT_EQ(41, tbq.r_query(tpi, day_idx_t{23U}));

  // update complete dominance
  tbq.r_update(tpi, 23, day1);
  EXPECT_EQ(23, tbq.r_query(tpi, day1));
  EXPECT_EQ(23, tbq.r_query(tpi, day2));
  EXPECT_EQ(23, tbq.r_query(tpi, day3));
  EXPECT_EQ(23, tbq.r_query(tpi, day_idx_t{23U}));

  // update no change
  tbq.r_update(tpi, 24, day1);
  EXPECT_EQ(23, tbq.r_query(tpi, day1));
  EXPECT_EQ(23, tbq.r_query(tpi, day2));
  EXPECT_EQ(23, tbq.r_query(tpi, day3));
  EXPECT_EQ(23, tbq.r_query(tpi, day_idx_t{23U}));

  // update partial dominance
  tbq.r_update(tpi, 24, day0);
  EXPECT_EQ(24, tbq.r_query(tpi, day0));
  EXPECT_EQ(23, tbq.r_query(tpi, day1));
  EXPECT_EQ(23, tbq.r_query(tpi, day2));
  EXPECT_EQ(23, tbq.r_query(tpi, day3));
  EXPECT_EQ(23, tbq.r_query(tpi, day_idx_t{23U}));
}

TEST(tb_query, enqueue) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, enqueue_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // init query
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  auto const si0{3U};
  day_idx_t const day_idx0{5U};
  tbq.enqueue(tpi0, si0, day_idx0, 0, TRANSFERRED_FROM_NULL);
  EXPECT_EQ(0, tbq.q_start_[0]);
  EXPECT_EQ(0, tbq.q_cur_[0]);
  EXPECT_EQ(1, tbq.q_end_[0]);
  ASSERT_EQ(1, tbq.q_.size());
  EXPECT_EQ(tpi0, tbq.q_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.q_[0].stop_idx_start_);
  EXPECT_EQ(5, tbq.q_[0].stop_idx_end_);
  EXPECT_EQ(day_idx0, tbq.q_[0].day_idx_);

  ASSERT_EQ(2, tbq.r_.size());
  day_idx_t const day_idx1{6U};
  EXPECT_EQ(si0, tbq.r_query(tpi0, day_idx0));
  EXPECT_EQ(si0, tbq.r_query(tpi0, day_idx1));
  transport_idx_t const tpi1{1U};
  EXPECT_EQ(si0, tbq.r_query(tpi1, day_idx0));
  EXPECT_EQ(si0, tbq.r_query(tpi1, day_idx1));

  auto const si1{2U};
  tbq.enqueue(tpi1, si1, day_idx0, 1, TRANSFERRED_FROM_NULL);
  EXPECT_EQ(0, tbq.q_start_[0]);
  EXPECT_EQ(0, tbq.q_cur_[0]);
  EXPECT_EQ(1, tbq.q_end_[0]);
  EXPECT_EQ(1, tbq.q_start_[1]);
  EXPECT_EQ(1, tbq.q_cur_[1]);
  EXPECT_EQ(2, tbq.q_end_[1]);
  ASSERT_EQ(2, tbq.q_.size());
  EXPECT_EQ(tpi0, tbq.q_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.q_[0].stop_idx_start_);
  EXPECT_EQ(5, tbq.q_[0].stop_idx_end_);
  EXPECT_EQ(day_idx0, tbq.q_[0].day_idx_);
  EXPECT_EQ(tpi1, tbq.q_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.q_[1].stop_idx_start_);
  EXPECT_EQ(si0, tbq.q_[1].stop_idx_end_);
  EXPECT_EQ(day_idx0, tbq.q_[1].day_idx_);
  ASSERT_EQ(2, tbq.r_.size());
  day_idx_t const day_idx23{23U};
  EXPECT_EQ(si1, tbq.r_query(tpi0, day_idx23));
  EXPECT_EQ(si0, tbq.r_query(tpi0, day_idx0));
  EXPECT_EQ(si1, tbq.r_query(tpi1, day_idx0));
}

#include <chrono>
using namespace std::chrono;

TEST(reconstruct_journey, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init query
  tb_query tbq{tbp};

  // create transport segment
  auto const day_idx0 =
      tt.day_idx(date::year_month_day{sys_days{March / 1 / 2021}});
  tb_query::transport_segment const tp_seg{transport_idx_t{0U}, 0U, 1U,
                                           day_idx0, TRANSFERRED_FROM_NULL};

  journey j{};
  tbq.reconstruct_journey(tp_seg, j);
  ASSERT_EQ(1, j.legs_.size());
  EXPECT_EQ(0, j.transfers_);
  EXPECT_EQ(location_idx_t{1U}, j.dest_);
  EXPECT_EQ(location_idx_t{0U}, j.legs_[0].from_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[0].to_);
  constexpr unixtime_t start_time_exp = sys_days{March / 1 / 2021};
  constexpr unixtime_t dest_time_exp = sys_days{March / 1 / 2021} + 12h;
  EXPECT_EQ(start_time_exp, j.start_time_);
  EXPECT_EQ(dest_time_exp, j.dest_time_);
}

TEST(reconstruct_journey, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, same_day_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init query
  tb_query tbq{tbp};

  // create transport segments
  auto const day_idx0 =
      tt.day_idx(date::year_month_day{sys_days{March / 1 / 2021}});
  tb_query::transport_segment const tp_seg0{transport_idx_t{0U}, 0U, 1U,
                                            day_idx0, TRANSFERRED_FROM_NULL};
  tb_query::transport_segment const tp_seg1{transport_idx_t{1U}, 0U, 1U,
                                            day_idx0, 0U};
  tbq.q_.emplace_back(tp_seg0);
  tbq.q_.emplace_back(tp_seg1);

  journey j{};
  tbq.reconstruct_journey(tp_seg1, j);
  ASSERT_EQ(3, j.legs_.size());
  EXPECT_EQ(1, j.transfers_);
  EXPECT_EQ(location_idx_t{2U}, j.dest_);
  EXPECT_EQ(location_idx_t{0U}, j.legs_[0].from_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[0].to_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[1].from_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[1].to_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[2].from_);
  EXPECT_EQ(location_idx_t{2U}, j.legs_[2].to_);
  constexpr unixtime_t start_time_exp = sys_days{March / 1 / 2021};
  constexpr unixtime_t dest_time_exp = sys_days{March / 1 / 2021} + 13h;
  EXPECT_EQ(start_time_exp, j.start_time_);
  EXPECT_EQ(dest_time_exp, j.dest_time_);
}

TEST(reconstruct_journey, transfer_with_footpath) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, footpath_files(), tt);

  // hack footpath into timetable, somehow it is not created from test data
  location_id const li_s1{"S1", src};
  auto const location_idx1 = tt.locations_.location_id_to_idx_.at(li_s1);
  location_id const li_s2{"S2", src};
  auto const location_idx2 = tt.locations_.location_id_to_idx_.at(li_s2);
  tt.locations_.footpaths_out_[location_idx1].emplace_back(
      footpath{location_idx2, duration_t{5U}});

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  ASSERT_EQ(1, tbp.ts_.transfers_.size());

  // init query
  tb_query tbq{tbp};

  // create transport segments
  auto const day_idx0 =
      tt.day_idx(date::year_month_day{sys_days{March / 1 / 2021}});
  tb_query::transport_segment const tp_seg0{transport_idx_t{0U}, 0U, 1U,
                                            day_idx0, TRANSFERRED_FROM_NULL};
  tb_query::transport_segment const tp_seg1{transport_idx_t{1U}, 0U, 1U,
                                            day_idx0, 0U};
  tbq.q_.emplace_back(tp_seg0);
  tbq.q_.emplace_back(tp_seg1);

  journey j{};
  tbq.reconstruct_journey(tp_seg1, j);

  ASSERT_EQ(3, j.legs_.size());
  EXPECT_EQ(1, j.transfers_);
  EXPECT_EQ(location_idx_t{3U}, j.dest_);
  EXPECT_EQ(location_idx_t{0U}, j.legs_[0].from_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[0].to_);
  constexpr unixtime_t start_time_leg0_exp = sys_days{March / 1 / 2021};
  constexpr unixtime_t dest_time_leg0_exp = sys_days{March / 1 / 2021} + 6h;
  EXPECT_EQ(start_time_leg0_exp, j.legs_[0].dep_time_);
  EXPECT_EQ(dest_time_leg0_exp, j.legs_[0].arr_time_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[1].from_);
  EXPECT_EQ(location_idx_t{2U}, j.legs_[1].to_);
  constexpr unixtime_t start_time_leg1_exp = sys_days{March / 1 / 2021} + 6h;
  constexpr unixtime_t dest_time_leg1_exp =
      sys_days{March / 1 / 2021} + 6h + 5min;
  EXPECT_EQ(start_time_leg1_exp, j.legs_[1].dep_time_);
  EXPECT_EQ(dest_time_leg1_exp, j.legs_[1].arr_time_);
  EXPECT_EQ(location_idx_t{2U}, j.legs_[2].from_);
  EXPECT_EQ(location_idx_t{3U}, j.legs_[2].to_);
  constexpr unixtime_t start_time_leg2_exp = sys_days{March / 1 / 2021} + 12h;
  constexpr unixtime_t dest_time_leg2_exp = sys_days{March / 1 / 2021} + 13h;
  EXPECT_EQ(start_time_leg2_exp, j.legs_[2].dep_time_);
  EXPECT_EQ(dest_time_leg2_exp, j.legs_[2].arr_time_);
  constexpr unixtime_t start_time_exp = sys_days{March / 1 / 2021};
  constexpr unixtime_t dest_time_exp = sys_days{March / 1 / 2021} + 13h;
  EXPECT_EQ(start_time_exp, j.start_time_);
  EXPECT_EQ(dest_time_exp, j.dest_time_);
}

constexpr auto const same_day_transfer_journeys = R"(
[2021-03-01 00:00, 2021-03-01 13:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S2, S2) [2021-03-01 13:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 06:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0 , day=2021-03-01, id=0/R0_MON, src=0}]
   1: S1      S1.............................................. a: 01.03 06:00 [01.03 06:00]
leg 1: (S1, S1) [2021-03-01 06:00] -> (S1, S1) [2021-03-01 06:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 12:00] -> (S2, S2) [2021-03-01 13:00]
   0: S1      S1..............................................                               d: 01.03 12:00 [01.03 12:00]  [{name=R1 , day=2021-03-01, id=0/R1_MON, src=0}]
   1: S2      S2.............................................. a: 01.03 13:00 [01.03 13:00]


)";

TEST(earliest_arrival_query, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, same_day_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init tb_query
  tb_query tbq(tbp);

  // construct input query
  query const q{
      .start_time_ = unixtime_t{sys_days{February / 28 / 2021} + 23h},
      .start_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .dest_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .use_start_footpaths_ = true,
      .start_ = {nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S0", .src_ = src}),
          0_minutes, 0U}},
      .destinations_ = {{nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S2", .src_ = src}),
          0_minutes, 0U}}},
      .via_destinations_ = {},
      .allowed_classes_ = bitset<kNumClasses>::max(),
      .max_transfers_ = 6U,
      .min_connection_count_ = 0U,
      .extend_interval_earlier_ = false,
      .extend_interval_later_ = false};

  // process query
  tbq.earliest_arrival_query(q);

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : tbq.j_) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{same_day_transfer_journeys}, ss.str());
}

constexpr auto const long_transfer_journeys = R"(
[2021-03-01 00:00, 2021-03-04 07:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S2, S2) [2021-03-04 07:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-04 04:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0 , day=2021-03-01, id=0/R0_MON, src=0}]
   1: S1      S1.............................................. a: 04.03 04:00 [04.03 04:00]
leg 1: (S1, S1) [2021-03-04 04:00] -> (S1, S1) [2021-03-04 04:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-04 06:00] -> (S2, S2) [2021-03-04 07:00]
   0: S1      S1..............................................                               d: 04.03 06:00 [04.03 06:00]  [{name=R1 , day=2021-03-04, id=0/R1_THU, src=0}]
   1: S2      S2.............................................. a: 04.03 07:00 [04.03 07:00]


)";

TEST(earliest_arrival_query, long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init tb_query
  tb_query tbq(tbp);

  // construct input query
  query const q{
      .start_time_ = unixtime_t{sys_days{February / 28 / 2021} + 23h},
      .start_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .dest_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .use_start_footpaths_ = true,
      .start_ = {nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S0", .src_ = src}),
          0_minutes, 0U}},
      .destinations_ = {{nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S2", .src_ = src}),
          0_minutes, 0U}}},
      .via_destinations_ = {},
      .allowed_classes_ = bitset<kNumClasses>::max(),
      .max_transfers_ = 6U,
      .min_connection_count_ = 0U,
      .extend_interval_earlier_ = false,
      .extend_interval_later_ = false};

  // process query
  tbq.earliest_arrival_query(q);

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : tbq.j_) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{long_transfer_journeys}, ss.str());
}

constexpr auto const earlier_stop_transfer_journeys = R"(
[2021-03-01 03:00, 2021-03-01 06:00]
TRANSFERS: 1
     FROM: (S3, S3) [2021-03-01 03:00]
       TO: (S2, S2) [2021-03-01 06:00]
leg 0: (S3, S3) [2021-03-01 03:00] -> (S1, S1) [2021-03-01 04:00]
   3: S3      S3..............................................                               d: 01.03 03:00 [01.03 03:00]  [{name=R0 , day=2021-03-01, id=0/R0_MON0, src=0}]
   4: S1      S1.............................................. a: 01.03 04:00 [01.03 04:00]
leg 1: (S1, S1) [2021-03-01 04:00] -> (S1, S1) [2021-03-01 04:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 05:00] -> (S2, S2) [2021-03-01 06:00]
   1: S1      S1..............................................                               d: 01.03 05:00 [01.03 05:00]  [{name=R0 , day=2021-03-01, id=0/R0_MON1, src=0}]
   2: S2      S2.............................................. a: 01.03 06:00 [01.03 06:00]


)";

TEST(earliest_arrival_query, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init tb_query
  tb_query tbq(tbp);

  // construct input query
  query const q{
      .start_time_ = unixtime_t{sys_days{February / 28 / 2021} + 23h},
      .start_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .dest_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .use_start_footpaths_ = true,
      .start_ = {nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S3", .src_ = src}),
          0_minutes, 0U}},
      .destinations_ = {{nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S2", .src_ = src}),
          0_minutes, 0U}}},
      .via_destinations_ = {},
      .allowed_classes_ = bitset<kNumClasses>::max(),
      .max_transfers_ = 6U,
      .min_connection_count_ = 0U,
      .extend_interval_earlier_ = false,
      .extend_interval_later_ = false};

  // process query
  tbq.earliest_arrival_query(q);

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : tbq.j_) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  EXPECT_EQ(std::string_view{earlier_stop_transfer_journeys}, ss.str());
}

TEST(earliest_arrival_query, no_journey_possible) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  // init tb_query
  tb_query tbq(tbp);

  // construct input query
  query const q{
      .start_time_ = unixtime_t{sys_days{February / 28 / 2021} + 23h},
      .start_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .dest_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .use_start_footpaths_ = true,
      .start_ = {nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S4", .src_ = src}),
          0_minutes, 0U}},
      .destinations_ = {{nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "S0", .src_ = src}),
          0_minutes, 0U}}},
      .via_destinations_ = {},
      .allowed_classes_ = bitset<kNumClasses>::max(),
      .max_transfers_ = 6U,
      .min_connection_count_ = 0U,
      .extend_interval_earlier_ = false,
      .extend_interval_later_ = false};

  // process query
  tbq.earliest_arrival_query(q);

  EXPECT_EQ(0, tbq.j_.size());
}

using namespace nigiri::test_data::hrd_timetable;

// constexpr auto const fwd_journeys = R"(
//[2020-03-30 05:00, 2020-03-30 07:15]
// TRANSFERS: 1
//      FROM: (A, 0000001) [2020-03-30 05:00]
//        TO: (C, 0000003) [2020-03-30 07:15]
// leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
//    0: 0000001 A............................................... d: 30.03 05:00
//    [30.03 07:00]  [{name=RE 1337, day=2020-03-30,
//    id=1337/0000001/300/0000002/360/, src=0}] 1: 0000002
//    B............................................... a: 30.03 06:00 [30.03
//    08:00]
// leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
//   FOOTPATH (duration=2)
// leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
//    0: 0000002 B............................................... d: 30.03 06:15
//    [30.03 08:15]  [{name=RE 7331, day=2020-03-30,
//    id=7331/0000002/375/0000003/435/, src=0}] 1: 0000003
//    C............................................... a: 30.03 07:15 [30.03
//    09:15]
// leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
//   FOOTPATH (duration=0)
//
//
//[2020-03-30 05:30, 2020-03-30 07:45]
// TRANSFERS: 1
//      FROM: (A, 0000001) [2020-03-30 05:30]
//        TO: (C, 0000003) [2020-03-30 07:45]
// leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
//    0: 0000001 A............................................... d: 30.03 05:30
//    [30.03 07:30]  [{name=RE 1337, day=2020-03-30,
//    id=1337/0000001/330/0000002/390/, src=0}] 1: 0000002
//    B............................................... a: 30.03 06:30 [30.03
//    08:30]
// leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
//   FOOTPATH (duration=2)
// leg 2: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
//    0: 0000002 B............................................... d: 30.03 06:45
//    [30.03 08:45]  [{name=RE 7331, day=2020-03-30,
//    id=7331/0000002/405/0000003/465/, src=0}] 1: 0000003
//    C............................................... a: 30.03 07:45 [30.03
//    09:45]
// leg 3: (C, 0000003) [2020-03-30 07:45] -> (C, 0000003) [2020-03-30 07:45]
//   FOOTPATH (duration=0)
//
//
//)";

TEST(earliest_arrival_query, files_abc) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  std::cerr << "num_footpaths: " << tt.locations_.footpaths_out_.size()
            << std::endl;

  auto const location_idx =
      tt.locations_.location_id_to_idx_.at({.id_ = "0000002", .src_ = src});
  for (auto const& fp : tt.locations_.footpaths_out_[location_idx]) {
    std::cerr << "footpath: " << fp.target_ << ", " << fp.duration_
              << std::endl;
  }

  tb_preprocessing tbp{tt};
  tbp.build_transfer_set(true, true);

  std::cerr << "num_transfers: " << tbp.ts_.transfers_.size() << std::endl;

  tb_query tbq{tbp};

  query const q{
      .start_time_ = unixtime_t{sys_days{March / 30 / 2020} + 5h},
      .start_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .dest_match_mode_ = nigiri::routing::location_match_mode::kExact,
      .use_start_footpaths_ = true,
      .start_ = {nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "0000001", .src_ = src}),
          0_minutes, 0U}},
      .destinations_ = {{nigiri::routing::offset{
          tt.locations_.location_id_to_idx_.at({.id_ = "0000003", .src_ = src}),
          0_minutes, 0U}}},
      .via_destinations_ = {},
      .allowed_classes_ = bitset<kNumClasses>::max(),
      .max_transfers_ = 6U,
      .min_connection_count_ = 0U,
      .extend_interval_earlier_ = false,
      .extend_interval_later_ = false};

  tbq.earliest_arrival_query(q);

  EXPECT_EQ(1, tbq.j_.size());

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : tbq.j_) {
    x.print(ss, tt);
    ss << "\n\n";
  }

  std::cerr << ss.str() << std::endl;
}