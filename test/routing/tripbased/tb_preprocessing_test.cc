#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "../../loader/hrd/hrd_timetable.h"

#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "tb_preprocessing_test.h"

#include "./test_data.h"

using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace nigiri::routing;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;
using namespace nigiri::test_data::hrd_timetable;

TEST(tripbased, get_or_create_bfi) {
  // init
  timetable tt;
  tb_preprocessor tbp{tt};

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

#ifdef TB_PREPRO_TRANSFER_REDUCTION
TEST(earliest_times, basic) {
  // init
  timetable tt;
  tb_preprocessor tbp{tt};
  tb_preprocessor::earliest_times ets{tbp};

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
  tb_preprocessor tbp{tt};
  tb_preprocessor::earliest_times ets{tbp};

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
  tb_preprocessor tbp{tt};
  tb_preprocessor::earliest_times ets{tbp};

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
  tb_preprocessor tbp{tt};
  tb_preprocessor::earliest_times ets{tbp};

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
  tb_preprocessor tbp{tt};
  tb_preprocessor::earliest_times ets{tbp};

  // fill params
  auto const num_updates = 100000U;
  auto const li_max = 100U;
  auto const time_max = 1440U;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> li_dist(0, li_max);
  std::uniform_int_distribution<> time_dist(0, time_max);
  std::uniform_int_distribution<unsigned long> bf_block_dist(
      0, std::numeric_limits<unsigned long>::max());

  // fill
  for (auto i = 0U; i < num_updates; ++i) {
    bitfield bf;
    for (auto j = 0U; j < bf.num_blocks; ++j) {
      bf.blocks_[j] = bf_block_dist(gen);
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
#endif

TEST(build_transfer_set, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, no_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(0, tbp.n_transfers_);
}

TEST(build_transfer_set, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, same_day_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const& transfers = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, from_long_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, long_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const transfers = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  bitfield const bf_tf_from_exp{"100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, weekday_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, weekday_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const transfers = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{1U}, t.bitfield_idx_);
  EXPECT_EQ(1, t.passes_midnight_);
  bitfield const bf_tf_from_exp{"0111100000"};
  EXPECT_EQ(bf_tf_from_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, daily_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, daily_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const& transfers = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  bitfield const bf_exp{"111111100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, earlier_stop_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, earlier_stop_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const& transfers = tbp.ts_.at(0U, 4U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, earlier_transport_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, earlier_transport_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  EXPECT_EQ(1, tbp.n_transfers_);
  auto const& transfers = tbp.ts_.at(1U, 1U);
  ASSERT_EQ(1, transfers.size());
  auto const& t = transfers[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{0U}, t.transport_idx_to_);
  EXPECT_EQ(4U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
}

TEST(build_transfer_set, uturn_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, uturn_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

  // transfer reduction removes a transfer of the test case on its own
#if defined(TB_PREPRO_UTURN_REMOVAL) || defined(TB_PREPRO_TRANSFER_REDUCTION)
  EXPECT_EQ(1, tbp.n_transfers_);
#else
  EXPECT_EQ(2, tbp.n_transfers_);
#endif

  bitfield const bf_exp{"100000"};

  // the earlier transfer
  auto const& transfers0 = tbp.ts_.at(0U, 1U);
  // transfer reduction on its own removes the earlier transfer instead of the
  // U-turn transfer
#if defined(TB_PREPRO_TRANSFER_REDUCTION) && !defined(TB_PREPRO_UTURN_REMOVAL)
  EXPECT_EQ(0, transfers0.size());
#else
  ASSERT_EQ(1, transfers0.size());
  auto const& t = transfers0[0];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(1U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
#endif

  // the U-turn transfer
  auto const& transfers1 = tbp.ts_.at(0U, 2U);
#ifdef TB_PREPRO_UTURN_REMOVAL
  EXPECT_EQ(0, transfers1.size());
#else
  ASSERT_EQ(1, transfers1.size());
  auto const& uturn_transfer = transfers1[0];
  EXPECT_EQ(transport_idx_t{1U}, uturn_transfer.transport_idx_to_);
  EXPECT_EQ(0U, uturn_transfer.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, uturn_transfer.bitfield_idx_);
  EXPECT_EQ(0, uturn_transfer.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[uturn_transfer.get_bitfield_idx()]);
#endif
}

TEST(build_transfer_set, unnecessary_transfer0) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, unnecessary0_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  EXPECT_EQ(0, tbp.n_transfers_);
#else
  EXPECT_EQ(1, tbp.n_transfers_);
  auto const& transfers0 = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers0.size());
  auto const& t = transfers0[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(0U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
#endif
}

TEST(build_transfer_set, unnecessary_transfer1) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0}, src, unnecessary1_transfer_files(), tt);
  finalize(tt);

  // init preprocessing
  tb_preprocessor tbp{tt};

  // run preprocessing
  tbp.build();

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  EXPECT_EQ(1, tbp.n_transfers_);

  auto const& transfers0 = tbp.ts_.at(0U, 2U);
  ASSERT_EQ(1, transfers0.size());
  auto const& t = transfers0[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(2U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
#else
  EXPECT_EQ(2, tbp.n_transfers_);

  auto const& transfers0 = tbp.ts_.at(0U, 1U);
  ASSERT_EQ(1, transfers0.size());
  auto const& t0 = transfers0[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t0.transport_idx_to_);
  EXPECT_EQ(1U, t0.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t0.bitfield_idx_);
  EXPECT_EQ(0, t0.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t0.get_bitfield_idx()]);

  auto const& transfers1 = tbp.ts_.at(0U, 2U);
  ASSERT_EQ(1, transfers1.size());
  auto const& t1 = transfers1[0];
  EXPECT_EQ(transport_idx_t{1U}, t1.transport_idx_to_);
  EXPECT_EQ(2U, t1.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t1.bitfield_idx_);
  EXPECT_EQ(0, t1.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t1.get_bitfield_idx()]);
#endif
}

TEST(transfer_set, serialize_deserialize) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  tb_preprocessor tbp{tt};
  tbp.build();

  auto ts_buf = cista::serialize(tbp.ts_);
  auto const ts_des =
      cista::deserialize<nvec<std::uint32_t, transfer, 2>>(ts_buf);

  ASSERT_EQ(tbp.ts_.size(), ts_des->size());
  for (auto t = 0U; t != ts_des->size(); ++t) {
    ASSERT_EQ(tbp.ts_.size(t), ts_des->size(t));
    for (auto s = 0U; s != ts_des->size(t); ++s) {
      ASSERT_EQ(tbp.ts_.at(t, s).size(), ts_des->at(t, s).size());
      auto const& transfers_expected = tbp.ts_.at(t, s);
      auto const& transfers_actual = ts_des->at(t, s);
      for (auto i = 0U; i != transfers_expected.size(); ++i) {
        EXPECT_TRUE(
            transfers_equal(transfers_expected[i], transfers_actual[i]));
      }
    }
  }
}

TEST(transfer_set, store_load) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  tb_preprocessor tbp{tt};
  tbp.build();
  tbp.store_transfer_set("test");

  timetable tt_loaded;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt_loaded);
  finalize(tt_loaded);
  tb_preprocessor tbp_loaded{tt_loaded};
  tbp_loaded.load_transfer_set("test");

  ASSERT_EQ(tbp.ts_.size(), tbp_loaded.ts_.size());
  for (auto t = 0U; t != tbp_loaded.ts_.size(); ++t) {
    ASSERT_EQ(tbp.ts_.size(t), tbp_loaded.ts_.size(t));
    for (auto s = 0U; s != tbp_loaded.ts_.size(t); ++s) {
      ASSERT_EQ(tbp.ts_.at(t, s).size(), tbp_loaded.ts_.at(t, s).size());
      auto const& transfers_expected = tbp.ts_.at(t, s);
      auto const& transfers_actual = tbp_loaded.ts_.at(t, s);
      for (auto i = 0U; i != transfers_expected.size(); ++i) {
        EXPECT_TRUE(
            transfers_equal(transfers_expected[i], transfers_actual[i]));
      }
    }
  }

  // clean up
  std::filesystem::remove("test.transfer_set");
  std::filesystem::remove("test.bitfields");
}