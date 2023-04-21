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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set();

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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set();

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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set(false);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
  tbp.build_transfer_set(true);

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
