#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"

#include "./test_data.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;

TEST(tripbased, get_or_create_bfi) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};

  // bitfield already registered with timetable
  bitfield const bf0{"0"};
  tt.register_bitfield(bf0);
  bitfield_idx_t const bfi0_exp{0U};
  EXPECT_EQ(tt.bitfields_[bfi0_exp], bf0);
  tbp.bitfield_to_bitfield_idx_.emplace(bf0, bfi0_exp);
  auto const bfi0_act = tbp.get_or_create_bfi(bf0);
  EXPECT_EQ(bfi0_exp, bfi0_act);

  // bitfield not yet registered with timetable
  bitfield const bf1{"1"};
  bitfield_idx_t const bfi1_exp{1U};
  auto const bfi1_act = tbp.get_or_create_bfi(bf1);
  ASSERT_EQ(bfi1_exp, bfi1_act);
  EXPECT_EQ(bf1, tt.bitfields_[bfi1_exp]);
}

TEST(earliest_times, basic) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_preprocessing::earliest_times ets{tbp};

  // update empty
  location_idx_t const li_23{23U};
  duration_t const time_42{42U};
  bitfield const bf_111{"111"};
  ets.update(li_23, time_42, bf_111);

  ASSERT_EQ(24, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());

  EXPECT_EQ(time_42, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);

  // update end
  location_idx_t const li_66{66U};
  duration_t const time_77{77U};
  bitfield const bf_101{"101"};
  ets.update(li_66, time_77, bf_101);

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_42, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_77, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);

  // update inner
  location_idx_t const li_55{55U};
  duration_t const time_88{88U};
  bitfield const bf_110{"110"};
  ets.update(li_55, time_88, bf_110);

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_55].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_42, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_88, ets.location_idx_times_[li_55][0].time_);
  EXPECT_EQ(bf_110, tt.bitfields_[ets.location_idx_times_[li_55][0].bf_idx_]);
  EXPECT_EQ(time_77, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);

  // update existing, addition, no overwrite
  duration_t const time_87{87U};
  bitfield const bf_010{"010"};
  ets.update(li_55, time_87, bf_010);
  bitfield const bf_100{"100"};

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(2, ets.location_idx_times_[li_55].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_42, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_88, ets.location_idx_times_[li_55][0].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets.location_idx_times_[li_55][0].bf_idx_]);
  EXPECT_EQ(time_87, ets.location_idx_times_[li_55][1].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li_55][1].bf_idx_]);
  EXPECT_EQ(time_77, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);

  // update existing, overwrite
  duration_t const time_86{86U};
  ets.update(li_55, time_86, bf_010);

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(2, ets.location_idx_times_[li_55].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_42, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_88, ets.location_idx_times_[li_55][0].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets.location_idx_times_[li_55][0].bf_idx_]);
  EXPECT_EQ(time_86, ets.location_idx_times_[li_55][1].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li_55][1].bf_idx_]);
  EXPECT_EQ(time_77, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);

  // update existing, overwrite
  duration_t const time_41{41U};
  ets.update(li_23, time_41, bf_111);

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(2, ets.location_idx_times_[li_55].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_41, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_88, ets.location_idx_times_[li_55][0].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets.location_idx_times_[li_55][0].bf_idx_]);
  EXPECT_EQ(time_86, ets.location_idx_times_[li_55][1].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li_55][1].bf_idx_]);
  EXPECT_EQ(time_77, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);

  // update existing, overwrite
  duration_t const time_76{76U};
  ets.update(li_66, time_76, bf_111);

  ASSERT_EQ(67, ets.size());
  ASSERT_EQ(1, ets.location_idx_times_[li_23].size());
  ASSERT_EQ(2, ets.location_idx_times_[li_55].size());
  ASSERT_EQ(1, ets.location_idx_times_[li_66].size());
  EXPECT_EQ(time_41, ets.location_idx_times_[li_23][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_23][0].bf_idx_]);
  EXPECT_EQ(time_88, ets.location_idx_times_[li_55][0].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets.location_idx_times_[li_55][0].bf_idx_]);
  EXPECT_EQ(time_86, ets.location_idx_times_[li_55][1].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li_55][1].bf_idx_]);
  EXPECT_EQ(time_76, ets.location_idx_times_[li_66][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li_66][0].bf_idx_]);
}

TEST(earliest_times, same_time_more_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_preprocessing::earliest_times ets{tbp};

  location_idx_t const li{23U};
  duration_t const time{42U};
  bitfield const bf_010{"010"};
  bitfield const bf_111{"111"};

  ets.update(li, time, bf_010);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);

  ets.update(li, time, bf_111);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);
}

TEST(earliest_times, same_time_less_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_preprocessing::earliest_times ets{tbp};

  location_idx_t const li{23U};
  duration_t const time{42U};
  bitfield const bf_111{"111"};
  bitfield const bf_101{"101"};

  ets.update(li, time, bf_111);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);

  ets.update(li, time, bf_101);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);
}

TEST(earliest_times, same_time_other_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_preprocessing::earliest_times ets{tbp};

  location_idx_t const li{23U};
  duration_t const time{42U};
  bitfield const bf_010{"010"};
  bitfield const bf_101{"101"};
  bitfield const bf_111{"111"};

  ets.update(li, time, bf_010);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);

  ets.update(li, time, bf_101);
  ASSERT_EQ(1, ets.location_idx_times_[li].size());
  EXPECT_EQ(time, ets.location_idx_times_[li][0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets.location_idx_times_[li][0].bf_idx_]);
}

TEST(earliest_times, random) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_preprocessing::earliest_times ets{tbp};

  // fill params
  auto const num_updates = 100000U;
  auto const li_max = 100U;
  auto const time_max = 1440U;
  auto const num_bits = 365U;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> li_dist(0, li_max);
  std::uniform_int_distribution<> time_dist(0, time_max);
  std::uniform_int_distribution<> bit_dist(0, 1);

  // fill
  for (auto i = 0U; i < num_updates; ++i) {
    bitfield bf;
    for (auto j = 0U; j < num_bits; ++j) {
      bf.set(j, bit_dist(gen));
    }
    ets.update(location_idx_t{li_dist(gen)}, duration_t{time_dist(gen)}, bf);
  }

  // check each time is unique per location
  // and bitsets are disjoint per location
  ASSERT_TRUE(0 < ets.size());
  unsigned num_entries{0U};
  std::set<duration_t> time_set;
  bitfield bf_or;
  for (auto const times : ets.location_idx_times_) {
    time_set.clear();
    bf_or = bitfield{"0"};
    for (auto const& et : times) {
      // if it has at least one active day it must be the only entry for this
      // time
      if (tt.bitfields_[et.bf_idx_].any()) {
        EXPECT_EQ(time_set.end(), time_set.find(et.time_));
        time_set.emplace(et.time_);
      }
      EXPECT_TRUE((bf_or & tt.bitfields_[et.bf_idx_]).none());
      bf_or |= tt.bitfields_[et.bf_idx_];
      ++num_entries;
    }
  }

  std::cout << "After performing " << num_updates
            << " random updates, the earliest times data structure has "
            << num_entries << " entries.\n";
}

using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;

TEST(initial_transfer_computation, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(0U, tbp.ts_.transfers_.size());
}

TEST(initial_transfer_computation, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, same_day_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, weekday_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_);
  EXPECT_EQ(1, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"0111100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, daily_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);
  ASSERT_EQ(1, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 4U);
  ASSERT_TRUE(transfers.has_value());

  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_transport_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{1U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);
  ASSERT_EQ(1, transfers->second);

  auto const t = tbp.ts_.transfers_[transfers->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(4U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, uturn_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, uturn_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(2, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);

  auto const transfers1 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_TRUE(transfers1.has_value());
  ASSERT_EQ(1, transfers1->first);
  ASSERT_EQ(2, transfers1->second);

  auto const uturn_transfer = tbp.ts_.transfers_[transfers1->first];
  EXPECT_EQ(transport_idx_t{1U}, uturn_transfer.transport_idx_to_);
  EXPECT_EQ(0U, uturn_transfer.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, uturn_transfer.bitfield_idx_);
  EXPECT_EQ(0, uturn_transfer.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[uturn_transfer.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, unnecessary_transfer0) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary0_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(initial_transfer_computation, unnecessary_transfer1) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary1_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(2, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t0 = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t0.transport_idx_to_);
  EXPECT_EQ(1U, t0.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t0.bitfield_idx_);
  EXPECT_EQ(0, t0.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t0.get_bitfield_idx()]);

  auto const transfers1 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_TRUE(transfers1.has_value());
  ASSERT_EQ(1, transfers1->first);
  ASSERT_EQ(2, transfers1->second);

  auto const t1 = tbp.ts_.transfers_[transfers1->first];
  EXPECT_EQ(transport_idx_t{1U}, t1.transport_idx_to_);
  EXPECT_EQ(2U, t1.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t1.bitfield_idx_);
  EXPECT_EQ(0, t1.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t1.get_bitfield_idx()]);
}

TEST(uturn_removal, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(0U, tbp.ts_.transfers_.size());
}

TEST(uturn_removal, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, same_day_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, weekday_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_);
  EXPECT_EQ(1, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"0111100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, daily_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 4U);
  ASSERT_TRUE(transfers.has_value());

  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_transport_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{1U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);
  ASSERT_EQ(1, transfers->second);

  auto const t = tbp.ts_.transfers_[transfers->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(4U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, uturn_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, uturn_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);

  auto const transfers1 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_FALSE(transfers1.has_value());
}

TEST(uturn_removal, unnecessary_transfer0) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary0_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(uturn_removal, unnecessary_transfer1) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary1_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(2, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t0 = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t0.transport_idx_to_);
  EXPECT_EQ(1U, t0.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t0.bitfield_idx_);
  EXPECT_EQ(0, t0.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t0.get_bitfield_idx()]);

  auto const transfers1 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_TRUE(transfers1.has_value());
  ASSERT_EQ(1, transfers1->first);
  ASSERT_EQ(2, transfers1->second);

  auto const t1 = tbp.ts_.transfers_[transfers1->first];
  EXPECT_EQ(transport_idx_t{1U}, t1.transport_idx_to_);
  EXPECT_EQ(2U, t1.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t1.bitfield_idx_);
  EXPECT_EQ(0, t1.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t1.get_bitfield_idx()]);
}

TEST(transfer_reduction, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(0U, tbp.ts_.transfers_.size());
}

TEST(transfer_reduction, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, same_day_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, weekday_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_);
  EXPECT_EQ(1, t.passes_midnight_);

  bitfield const bf_tf_from_exp{"0111100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, daily_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{0U}, 4U);
  ASSERT_TRUE(transfers.has_value());

  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_transport_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers = tbp.ts_.get_transfers(transport_idx_t{1U}, 1U);
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);
  ASSERT_EQ(1, transfers->second);

  auto const t = tbp.ts_.transfers_[transfers->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(4U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(transfer_reduction, uturn_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, uturn_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 1U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);

  auto const transfers1 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_FALSE(transfers1.has_value());
}

TEST(transfer_reduction, unnecessary_transfer0) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary0_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(0, tbp.ts_.transfers_.size());
}

TEST(transfer_reduction, unnecessary_transfer1) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, unnecessary1_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, true);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers0 = tbp.ts_.get_transfers(transport_idx_t{0U}, 2U);
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(2U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}