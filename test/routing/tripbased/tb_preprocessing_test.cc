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
  EXPECT_EQ(bfi1_exp, bfi1_act);
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
  tbp.initial_transfer_computation();

  ASSERT_EQ(0, tbp.ts_.transfers_.size());
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
  tbp.initial_transfer_computation();

  ASSERT_EQ(1, tbp.ts_.transfers_.size());
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
  tbp.initial_transfer_computation();

  ASSERT_EQ(1, tbp.ts_.transfers_.size());
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
  tbp.initial_transfer_computation();

  ASSERT_EQ(1, tbp.ts_.transfers_.size());
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
}
