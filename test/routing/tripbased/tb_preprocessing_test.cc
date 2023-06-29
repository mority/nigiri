#include <filesystem>
#include <random>

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "../../loader/hrd/hrd_timetable.h"

#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "nigiri/routing/tripbased/transfer_set.h"
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

TEST(build_transfer_set, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, no_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(0, ts.n_transfers_);
}

TEST(build_transfer_set, same_day_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = gtfs_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(loader_config{0, "Etc/UTC"}, src, same_day_transfer_files(),
                 tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const& transfers = ts.at(0U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src, long_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const transfers = ts.at(0U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src, weekday_transfer_files(),
                 tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const transfers = ts.at(0U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src, daily_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const& transfers = ts.at(0U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 earlier_stop_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const& transfers = ts.at(0U, 4U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 earlier_transport_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  EXPECT_EQ(1, ts.n_transfers_);
  auto const& transfers = ts.at(1U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src, uturn_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

  // transfer reduction removes a transfer of the test case on its own
#if defined(TB_PREPRO_UTURN_REMOVAL) || defined(TB_PREPRO_TRANSFER_REDUCTION)
  EXPECT_EQ(1, ts.n_transfers_);
#else
  EXPECT_EQ(2, ts.n_transfers_);
#endif

  bitfield const bf_exp{"100000"};

  // the earlier transfer
  auto const& transfers0 = ts.at(0U, 1U);
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
  auto const& transfers1 = ts.at(0U, 2U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 unnecessary0_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  EXPECT_EQ(0, ts.n_transfers_);
#else
  EXPECT_EQ(1, ts.n_transfers_);
  auto const& transfers0 = ts.at(0U, 1U);
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
  load_timetable(loader_config{0, "Etc/UTC"}, src,
                 unnecessary1_transfer_files(), tt);
  finalize(tt);

  // run preprocessing
  transfer_set ts;
  build_transfer_set(tt, ts);

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  EXPECT_EQ(1, ts.n_transfers_);

  auto const& transfers0 = ts.at(0U, 2U);
  ASSERT_EQ(1, transfers0.size());
  auto const& t = transfers0[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(2U, t.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_);
  EXPECT_EQ(0, t.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t.get_bitfield_idx()]);
#else
  EXPECT_EQ(2, ts.n_transfers_);

  auto const& transfers0 = ts.at(0U, 1U);
  ASSERT_EQ(1, transfers0.size());
  auto const& t0 = transfers0[0];
  bitfield const bf_exp{"100000"};
  EXPECT_EQ(transport_idx_t{1U}, t0.transport_idx_to_);
  EXPECT_EQ(1U, t0.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t0.bitfield_idx_);
  EXPECT_EQ(0, t0.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t0.get_bitfield_idx()]);

  auto const& transfers1 = ts.at(0U, 2U);
  ASSERT_EQ(1, transfers1.size());
  auto const& t1 = transfers1[0];
  EXPECT_EQ(transport_idx_t{1U}, t1.transport_idx_to_);
  EXPECT_EQ(2U, t1.stop_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t1.bitfield_idx_);
  EXPECT_EQ(0, t1.passes_midnight_);
  EXPECT_EQ(bf_exp, tt.bitfields_[t1.get_bitfield_idx()]);
#endif
}

TEST(build_transfer_set, serialization) {
  constexpr auto const src = source_idx_t{0U};
  timetable tt;
  tt.date_range_ = full_period();
  register_special_stations(tt);
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);

  std::shared_ptr<cista::wrapped<transfer_set>> const ts =
      std::make_shared<cista::wrapped<transfer_set>>(
          cista::raw::make_unique<transfer_set>());

  build_transfer_set(tt, **ts);

  std::filesystem::path const ts_file_name{"test.transfer_set"};
  (*ts)->write(ts_file_name);

  auto ts_loaded = std::make_shared<cista::wrapped<transfer_set>>(
      transfer_set::read(cista::memory_holder{
          cista::file{ts_file_name.string().c_str(), "r"}.content()}));

  EXPECT_EQ((*ts_loaded)->tt_hash_, hash_tt(tt));
  EXPECT_EQ((*ts)->hash(), (*ts_loaded)->hash());

  ASSERT_EQ((*ts)->data_.size(), (*ts_loaded)->data_.size());
  for (std::uint32_t t = 0U; t != (*ts)->data_.size(); ++t) {
    ASSERT_EQ((*ts)->data_.size(t), (*ts_loaded)->data_.size(t));
    for (std::uint32_t l = 0U; l != (*ts)->data_.size(t); ++l) {
      ASSERT_EQ((*ts)->data_.at(t, l).size(),
                (*ts_loaded)->data_.at(t, l).size());
      for (std::uint32_t tr = 0U; tr != (*ts)->data_.at(t, l).size(); ++tr) {
        EXPECT_TRUE(transfers_equal((*ts)->data_.at(t, l)[tr],
                                    (*ts_loaded)->data_.at(t, l)[tr]));
      }
    }
  }

  std::filesystem::remove(ts_file_name);
}