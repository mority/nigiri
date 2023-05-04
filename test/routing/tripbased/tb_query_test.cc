#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/routing/tripbased/tb_query.h"

#include "./test_data.h"

using namespace nigiri;
using namespace nigiri::loader::gtfs;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;

TEST(r_, basic) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  // update empty
  transport_idx_t const tpi0{22U};
  unsigned const si0{42U};
  bitfield const bf_010{"010"};
  tbq.r_update(tpi0, si0, bf_010);
  ASSERT_EQ(1, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);

  // update end
  transport_idx_t const tpi1{24U};
  unsigned const si1{41U};
  bitfield const bf_110{"110"};
  tbq.r_update(tpi1, si1, bf_110);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  // update inner
  transport_idx_t const tpi2{23U};
  unsigned const si2{11U};
  bitfield const bf_001{"001"};
  tbq.r_update(tpi2, si2, bf_001);
  ASSERT_EQ(3, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi2, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si2, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_001, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[2].bitfield_idx_]);

  // update existing inner, no remove
  transport_idx_t const tpi3{23U};
  unsigned const si3{12U};
  bitfield const bf_100{"100"};
  tbq.r_update(tpi3, si3, bf_100);
  ASSERT_EQ(4, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi2, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si2, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_001, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi3, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si3, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_100, tt.bitfields_[tbq.r_[2].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[3].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[3].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[3].bitfield_idx_]);

  // update existing inner, remove
  transport_idx_t const tpi4{23U};
  unsigned const si4{10U};
  bitfield const bf_111{"111"};
  tbq.r_update(tpi4, si4, bf_111);
  ASSERT_EQ(3, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi4, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si4, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_111, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[2].bitfield_idx_]);

  // r_query
  EXPECT_EQ(si4, tbq.r_query(tpi4, bf_010));
  EXPECT_EQ(si0, tbq.r_query(tpi0, bf_010));
  EXPECT_EQ(si1, tbq.r_query(tpi1, bf_100));
}

TEST(r_update, same_stop_more_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"1"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"11"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf2, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, same_stop_less_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"11"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"1"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, same_stop_other_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"01"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"10"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  bitfield const bf3{"11"};
  EXPECT_EQ(bf3, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, random) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  // fill params
  auto const num_updates = 100000U;
  auto const tpi_max = 100U;
  auto const si_max = 23U;
  auto const num_bits = 365U;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> tpi_dist(0, tpi_max);
  std::uniform_int_distribution<> si_dist(0, si_max);
  std::uniform_int_distribution<> bit_dist(0, 1);

  // fill
  for (auto i = 0U; i < num_updates; ++i) {
    bitfield bf;
    for (auto j = 0U; j < num_bits; ++j) {
      bf.set(j, bit_dist(gen));
    }
    tbq.r_update(transport_idx_t{tpi_dist(gen)},
                 static_cast<unsigned>(si_dist(gen)), bf);
  }

  // check sorted by transport
  for (auto it = tbq.r_.begin(); it + 1 != tbq.r_.end(); ++it) {
    EXPECT_TRUE(it->transport_idx_ <= (it + 1)->transport_idx_);
  }

  // check each stop is unique per transport
  ASSERT_TRUE(0 < tbq.r_.size());
  transport_idx_t tpi_cur = tbq.r_[0].transport_idx_;
  std::set<unsigned> si_set;
  for (auto it = tbq.r_.begin(); it != tbq.r_.end(); ++it) {
    if (it->transport_idx_ != tpi_cur) {
      tpi_cur = it->transport_idx_;
      si_set.clear();
    }
    EXPECT_EQ(si_set.end(), si_set.find(it->stop_idx_));
    si_set.emplace(it->stop_idx_);
  }

  // check bitsets are disjoint per transport
  tpi_cur = tbq.r_[0].transport_idx_;
  auto bf_or = tt.bitfields_[tbq.r_[0].bitfield_idx_];
  for (auto it = tbq.r_.begin() + 1; it != tbq.r_.end(); ++it) {
    if (it->transport_idx_ != tpi_cur) {
      tpi_cur = it->transport_idx_;
      bf_or = tt.bitfields_[it->bitfield_idx_];
    } else {
      EXPECT_TRUE((bf_or & tt.bitfields_[it->bitfield_idx_]).none());
      bf_or |= tt.bitfields_[it->bitfield_idx_];
    }
  }
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

  // transferred from
  tb_query::transport_segment transferred_from(transport_idx_t{23U}, 42U, 43U,
                                               bitfield_idx_t{0U}, nullptr);

  transport_idx_t const tpi0{0U};
  auto const si0{3U};
  bitfield const bf0{"100000"};
  tbq.enqueue(tpi0, si0, bf0, 0, &transferred_from);
  EXPECT_EQ(0, tbq.q_start_[0]);
  EXPECT_EQ(0, tbq.q_cur_[0]);
  EXPECT_EQ(1, tbq.q_end_[0]);
  ASSERT_EQ(1, tbq.q_.size());
  EXPECT_EQ(tpi0, tbq.q_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.q_[0].stop_idx_start_);
  EXPECT_EQ(5, tbq.q_[0].stop_idx_end_);
  EXPECT_EQ(tbp.get_or_create_bfi(bf0), tbq.q_[0].bitfield_idx_);

  ASSERT_EQ(2, tbq.r_.size());
  bitfield const bf1{"1000000"};
  EXPECT_EQ(si0, tbq.r_query(tpi0, bf0));
  EXPECT_EQ(si0, tbq.r_query(tpi0, bf1));
  transport_idx_t const tpi1{1U};
  EXPECT_EQ(si0, tbq.r_query(tpi1, bf0));
  EXPECT_EQ(si0, tbq.r_query(tpi1, bf1));

  auto const si1{2U};
  tbq.enqueue(tpi1, si1, bf0, 1, &transferred_from);
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
  EXPECT_EQ(tbp.get_or_create_bfi(bf0), tbq.q_[0].bitfield_idx_);
  EXPECT_EQ(tpi1, tbq.q_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.q_[1].stop_idx_start_);
  EXPECT_EQ(si0, tbq.q_[1].stop_idx_end_);
  EXPECT_EQ(tbp.get_or_create_bfi(bf0), tbq.q_[1].bitfield_idx_);
  ASSERT_EQ(3, tbq.r_.size());
  bitfield const bf2{"10000000000000000000000"};
  EXPECT_EQ(si1, tbq.r_query(tpi0, bf2));
  EXPECT_EQ(si0, tbq.r_query(tpi0, bf0));
  EXPECT_EQ(si1, tbq.r_query(tpi1, bf0));
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
  tb_query::transport_segment const tp_seg{transport_idx_t{0U}, 0U, 1U,
                                           bitfield_idx_t{0U}, nullptr};

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
  tb_query::transport_segment const tp_seg0{transport_idx_t{0U}, 0U, 1U,
                                            bitfield_idx_t{0U}, nullptr};
  tb_query::transport_segment const tp_seg1{transport_idx_t{1U}, 0U, 1U,
                                            bitfield_idx_t{0U}, &tp_seg0};

  journey j{};
  tbq.reconstruct_journey(tp_seg1, j);
  ASSERT_EQ(2, j.legs_.size());
  EXPECT_EQ(1, j.transfers_);
  EXPECT_EQ(location_idx_t{2U}, j.dest_);
  EXPECT_EQ(location_idx_t{0U}, j.legs_[0].from_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[0].to_);
  EXPECT_EQ(location_idx_t{1U}, j.legs_[1].from_);
  EXPECT_EQ(location_idx_t{2U}, j.legs_[1].to_);
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
      footpath{location_idx2, duration_t{5}});

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  ASSERT_EQ(1, tbp.ts_.transfers_.size());

  // init query
  tb_query tbq{tbp};

  // create transport segments
  tb_query::transport_segment const tp_seg0{transport_idx_t{0U}, 0U, 1U,
                                            bitfield_idx_t{0U}, nullptr};
  tb_query::transport_segment const tp_seg1{transport_idx_t{1U}, 0U, 1U,
                                            bitfield_idx_t{0U}, &tp_seg0};

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
}
