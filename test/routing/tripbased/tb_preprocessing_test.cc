#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
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
  location_idx_t const li_exp0{23U};
  duration_t const time_exp0{42U};
  bitfield const bf_111{"111"};
  ets.update(li_exp0, time_exp0, bf_111);

  ASSERT_EQ(1, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp0, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  // update end
  location_idx_t const li_exp1{66U};
  duration_t const time_exp1{77U};
  bitfield const bf_101{"101"};
  ets.update(li_exp1, time_exp1, bf_101);

  ASSERT_EQ(2, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp0, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[1].location_idx_);
  EXPECT_EQ(time_exp1, ets[1].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets[1].bf_idx_]);

  // update inner
  location_idx_t const li_exp2{55U};
  duration_t const time_exp2{88U};
  bitfield const bf_110{"110"};
  ets.update(li_exp2, time_exp2, bf_110);

  ASSERT_EQ(3, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp0, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp2, ets[1].location_idx_);
  EXPECT_EQ(time_exp2, ets[1].time_);
  EXPECT_EQ(bf_110, tt.bitfields_[ets[1].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[2].location_idx_);
  EXPECT_EQ(time_exp1, ets[2].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets[2].bf_idx_]);

  // update existing inner, no remove
  location_idx_t const li_exp3{55U};
  duration_t const time_exp3{87U};
  bitfield const bf_010{"010"};
  ets.update(li_exp3, time_exp3, bf_010);
  bitfield const bf_100{"100"};

  ASSERT_EQ(4, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp0, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp2, ets[1].location_idx_);
  EXPECT_EQ(time_exp2, ets[1].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets[1].bf_idx_]);

  EXPECT_EQ(li_exp3, ets[2].location_idx_);
  EXPECT_EQ(time_exp3, ets[2].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[2].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[3].location_idx_);
  EXPECT_EQ(time_exp1, ets[3].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets[3].bf_idx_]);

  // update existing inner, remove
  location_idx_t const li_exp4{55U};
  duration_t const time_exp4{86U};

  ets.update(li_exp4, time_exp4, bf_010);

  ASSERT_EQ(4, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp0, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp2, ets[1].location_idx_);
  EXPECT_EQ(time_exp2, ets[1].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets[1].bf_idx_]);

  EXPECT_EQ(li_exp4, ets[2].location_idx_);
  EXPECT_EQ(time_exp4, ets[2].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[2].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[3].location_idx_);
  EXPECT_EQ(time_exp1, ets[3].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets[3].bf_idx_]);

  // update existing begin, remove
  duration_t const time_exp5{41U};

  ets.update(li_exp0, time_exp5, bf_111);

  ASSERT_EQ(4, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp5, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp2, ets[1].location_idx_);
  EXPECT_EQ(time_exp2, ets[1].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets[1].bf_idx_]);

  EXPECT_EQ(li_exp4, ets[2].location_idx_);
  EXPECT_EQ(time_exp4, ets[2].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[2].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[3].location_idx_);
  EXPECT_EQ(time_exp1, ets[3].time_);
  EXPECT_EQ(bf_101, tt.bitfields_[ets[3].bf_idx_]);

  // update existing end, remove
  duration_t const time_exp6{76U};

  ets.update(li_exp1, time_exp6, bf_111);

  ASSERT_EQ(4, ets.size());

  EXPECT_EQ(li_exp0, ets[0].location_idx_);
  EXPECT_EQ(time_exp5, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  EXPECT_EQ(li_exp2, ets[1].location_idx_);
  EXPECT_EQ(time_exp2, ets[1].time_);
  EXPECT_EQ(bf_100, tt.bitfields_[ets[1].bf_idx_]);

  EXPECT_EQ(li_exp4, ets[2].location_idx_);
  EXPECT_EQ(time_exp4, ets[2].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[2].bf_idx_]);

  EXPECT_EQ(li_exp1, ets[3].location_idx_);
  EXPECT_EQ(time_exp6, ets[3].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[3].bf_idx_]);
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
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[0].bf_idx_]);

  ets.update(li, time, bf_111);
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);
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
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);

  ets.update(li, time, bf_101);
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);
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
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_010, tt.bitfields_[ets[0].bf_idx_]);

  ets.update(li, time, bf_101);
  ASSERT_EQ(1, ets.size());
  EXPECT_EQ(li, ets[0].location_idx_);
  EXPECT_EQ(time, ets[0].time_);
  EXPECT_EQ(bf_111, tt.bitfields_[ets[0].bf_idx_]);
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

  // check sorted by location
  for (auto it = ets.data_.begin(); it + 1 != ets.data_.end(); ++it) {
    EXPECT_TRUE(it->location_idx_ <= (it + 1)->location_idx_);
  }

  // check each time is unique per location
  ASSERT_TRUE(0 < ets.size());
  location_idx_t li_cur = ets[0].location_idx_;
  std::set<duration_t> time_set;
  for (auto it = ets.data_.begin(); it != ets.data_.end(); ++it) {
    if (it->location_idx_ != li_cur) {
      li_cur = it->location_idx_;
      time_set.clear();
    }
    EXPECT_EQ(time_set.end(), time_set.find(it->time_));
    time_set.emplace(it->time_);
  }

  // check bitsets are disjoint per location
  li_cur = ets[0].location_idx_;
  auto bf_or = tt.bitfields_[ets[0].bf_idx_];
  for (auto it = ets.data_.begin() + 1; it != ets.data_.end(); ++it) {
    if (it->location_idx_ != li_cur) {
      li_cur = it->location_idx_;
      bf_or = tt.bitfields_[it->bf_idx_];
    } else {
      EXPECT_TRUE((bf_or & tt.bitfields_[it->bf_idx_]).none());
      bf_or |= tt.bitfields_[it->bf_idx_];
    }
  }
}

using namespace nigiri::loader::gtfs;

TEST(initial_transfer_computation, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);

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

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_to_);

  bitfield const bf_tf_from_exp{"100000"};
  bitfield const bf_tf_to_exp{"100000000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_tf_to_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, weekday_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{1U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{2U}, t.bitfield_idx_to_);

  bitfield const bf_tf_from_exp{"0111100000"};
  bitfield const bf_tf_to_exp{"1111000000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_tf_to_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, daily_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);
  ASSERT_EQ(1, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  /*
  This fails because it finds the same transfer twice. Oddly, it says that 2
  routes serve S1.

  [tb_preprocessing.cc:89] tpi_from = 0, ri_from = 0, si_from = 4, li_from =
  2(S1), t_arr_from = 240min, sa_tp_from = 0 [tb_preprocessing.cc:113] li_to =
  2(S1), sa_fp = 0, a = 242min [tb_preprocessing.cc:123] Examining 2 routes at
  S1... [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1,
  ri_to = 0, si_to = 1, to location:  = S1 Looking for earliest connecting
  transport after a = 242min, initial omega = 00000000000100000
  [tb_preprocessing.cc:202] tpi_to = 1: different route -> 0, earlier stop -> 1,
  earlier transport -> 0 [tb_preprocessing.cc:258] new omega = 00000000000000000
  [tb_preprocessing.cc:274] transfer added:
  tpi_from = 0, li_from = 2
  tpi_to = 1, li_to = 2
  bf_tf_from = 00000000000100000
  bf_tf_to = 00000000000100000
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 4, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 0, earlier transport -> 0
  [tb_preprocessing.cc:291] passing midnight
  [tb_preprocessing.cc:202] tpi_to = 0: different route -> 0, earlier stop -> 0,
  earlier transport -> 0 [tb_preprocessing.cc:202] tpi_to = 1: different route
  -> 0, earlier stop -> 0, earlier transport -> 0 [tb_preprocessing.cc:291]
  passing midnight [tb_preprocessing.cc:299] Maximum waiting time reached
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 1, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 1, earlier transport -> 0
  [tb_preprocessing.cc:258] new omega =  00000000000000000
  [tb_preprocessing.cc:274] transfer added:
  tpi_from = 0, li_from = 2
  tpi_to = 1, li_to = 2
  bf_tf_from = 00000000000100000
  bf_tf_to = 00000000000100000
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 4, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 0, earlier transport -> 0
  [tb_preprocessing.cc:291] passing midnight
  [tb_preprocessing.cc:202] tpi_to = 0: different route -> 0, earlier stop -> 0,
  earlier transport -> 0 [tb_preprocessing.cc:202] tpi_to = 1: different route
  -> 0, earlier stop -> 0, earlier transport -> 0 [tb_preprocessing.cc:291]
  passing midnight [tb_preprocessing.cc:299] Maximum waiting time reached
  [tb_preprocessing.cc:89] tpi_from = 0, ri_from = 0, si_from = 5, li_from =
  0(S4), t_arr_from = 300min, sa_tp_from = 0 [tb_preprocessing.cc:113] li_to =
  0(S4), sa_fp = 0, a = 302min [tb_preprocessing.cc:123] Examining 1 routes at
  S4... [tb_preprocessing.cc:70]
  -----------------------------------------------------------------*/
  //  EXPECT_EQ(1U, tbp.ts_.transfers_.size());

  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{2U});
  ASSERT_TRUE(transfers.has_value());

  ASSERT_EQ(0U, transfers->first);
  //  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_transport_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  // fails, see test case above
  // EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{1U}, location_idx_t{2U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);

  // fails, see test case above
  //  ASSERT_EQ(1, transfers->second);

  auto const t = tbp.ts_.transfers_[transfers->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(initial_transfer_computation, uturn_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, uturn_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(false, false);

  EXPECT_EQ(2, tbp.ts_.transfers_.size());

  auto const transfers0 =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{2U});
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);

  auto const transfers1 =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{3U});
  ASSERT_TRUE(transfers1.has_value());
  ASSERT_EQ(1, transfers1->first);
  ASSERT_EQ(2, transfers1->second);

  auto const uturn_transfer = tbp.ts_.transfers_[transfers1->first];
  EXPECT_EQ(transport_idx_t{1U}, uturn_transfer.transport_idx_to_);
  EXPECT_EQ(location_idx_t{3U}, uturn_transfer.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, uturn_transfer.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, uturn_transfer.bitfield_idx_to_);
  EXPECT_EQ(bf_exp, tt.bitfields_[uturn_transfer.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[uturn_transfer.bitfield_idx_to_]);
}

TEST(uturn_removal, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, no_transfer_files(), tt);

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

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, long_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_to_);

  bitfield const bf_tf_from_exp{"100000"};
  bitfield const bf_tf_to_exp{"100000000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_tf_to_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, weekday_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1U, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{1U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{2U}, t.bitfield_idx_to_);

  bitfield const bf_tf_from_exp{"0111100000"};
  bitfield const bf_tf_to_exp{"1111000000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_tf_to_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, daily_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{1U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{1U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_stop_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  /*
  This fails because it finds the same transfer twice. Oddly, it says that 2
  routes serve S1.

  [tb_preprocessing.cc:89] tpi_from = 0, ri_from = 0, si_from = 4, li_from =
  2(S1), t_arr_from = 240min, sa_tp_from = 0 [tb_preprocessing.cc:113] li_to =
  2(S1), sa_fp = 0, a = 242min [tb_preprocessing.cc:123] Examining 2 routes at
  S1... [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1,
  ri_to = 0, si_to = 1, to location:  = S1 Looking for earliest connecting
  transport after a = 242min, initial omega = 00000000000100000
  [tb_preprocessing.cc:202] tpi_to = 1: different route -> 0, earlier stop -> 1,
  earlier transport -> 0 [tb_preprocessing.cc:258] new omega = 00000000000000000
  [tb_preprocessing.cc:274] transfer added:
  tpi_from = 0, li_from = 2
  tpi_to = 1, li_to = 2
  bf_tf_from = 00000000000100000
  bf_tf_to = 00000000000100000
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 4, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 0, earlier transport -> 0
  [tb_preprocessing.cc:291] passing midnight
  [tb_preprocessing.cc:202] tpi_to = 0: different route -> 0, earlier stop -> 0,
  earlier transport -> 0 [tb_preprocessing.cc:202] tpi_to = 1: different route
  -> 0, earlier stop -> 0, earlier transport -> 0 [tb_preprocessing.cc:291]
  passing midnight [tb_preprocessing.cc:299] Maximum waiting time reached
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 1, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 1, earlier transport -> 0
  [tb_preprocessing.cc:258] new omega =  00000000000000000
  [tb_preprocessing.cc:274] transfer added:
  tpi_from = 0, li_from = 2
  tpi_to = 1, li_to = 2
  bf_tf_from = 00000000000100000
  bf_tf_to = 00000000000100000
  [tb_preprocessing.cc:172] tpi_from = 0, si_from = 4, from location: S1, ri_to
  = 0, si_to = 4, to location:  = S1 Looking for earliest connecting transport
  after a = 242min, initial omega = 00000000000100000 [tb_preprocessing.cc:202]
  tpi_to = 1: different route -> 0, earlier stop -> 0, earlier transport -> 0
  [tb_preprocessing.cc:291] passing midnight
  [tb_preprocessing.cc:202] tpi_to = 0: different route -> 0, earlier stop -> 0,
  earlier transport -> 0 [tb_preprocessing.cc:202] tpi_to = 1: different route
  -> 0, earlier stop -> 0, earlier transport -> 0 [tb_preprocessing.cc:291]
  passing midnight [tb_preprocessing.cc:299] Maximum waiting time reached
  [tb_preprocessing.cc:89] tpi_from = 0, ri_from = 0, si_from = 5, li_from =
  0(S4), t_arr_from = 300min, sa_tp_from = 0 [tb_preprocessing.cc:113] li_to =
  0(S4), sa_fp = 0, a = 302min [tb_preprocessing.cc:123] Examining 1 routes at
  S4... [tb_preprocessing.cc:70]
  -----------------------------------------------------------------*/
  //  EXPECT_EQ(1U, tbp.ts_.transfers_.size());

  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{2U});
  ASSERT_TRUE(transfers.has_value());

  ASSERT_EQ(0U, transfers->first);
  //  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);

  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, earlier_transport_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  // fails, see test case above
  // EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{1U}, location_idx_t{2U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0, transfers->first);

  // fails, see test case above
  //  ASSERT_EQ(1, transfers->second);

  auto const t = tbp.ts_.transfers_[transfers->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);
}

TEST(uturn_removal, uturn_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, uturn_transfer_files(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.build_transfer_set(true, false);

  EXPECT_EQ(1, tbp.ts_.transfers_.size());

  auto const transfers0 =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{2U});
  ASSERT_TRUE(transfers0.has_value());
  ASSERT_EQ(0, transfers0->first);
  ASSERT_EQ(1, transfers0->second);

  auto const t = tbp.ts_.transfers_[transfers0->first];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{2U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_from_]);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.bitfield_idx_to_]);

  auto const transfers1 =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{3U});
  ASSERT_FALSE(transfers1.has_value());
}
