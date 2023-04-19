#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"

#include "nigiri/print_transport.h"

#include "../../loader/hrd/hrd_timetable.h"

#include "./test_data.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace nigiri::routing::tripbased::test;

constexpr interval<std::chrono::sys_days> hrd_full_period() {
  using namespace date;
  constexpr auto const from = (2020_y / March / 28).operator sys_days();
  constexpr auto const to = (2020_y / March / 31).operator sys_days();
  return {from, to};
}

constexpr interval<std::chrono::sys_days> gtfs_full_period() {
  using namespace date;
  constexpr auto const from = (2021_y / March / 01).operator sys_days();
  constexpr auto const to = (2021_y / March / 07).operator sys_days();
  return {from, to};
}

TEST(tripbased, get_or_create_bfi) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};

  // bitfield already registered with timetable
  bitfield bf0{"0"};
  tt.register_bitfield(bf0);
  bitfield_idx_t bfi0_exp{0U};
  EXPECT_EQ(tt.bitfields_[bfi0_exp], bf0);
  tbp.bitfield_to_bitfield_idx_.emplace(bf0, bfi0_exp);
  bitfield_idx_t bfi0_act = tbp.get_or_create_bfi(bf0);
  EXPECT_EQ(bfi0_exp, bfi0_act);

  // bitfield not yet registered with timetable
  bitfield bf1{"1"};
  bitfield_idx_t bfi1_exp{1U};
  bitfield_idx_t bfi1_act = tbp.get_or_create_bfi(bf1);
  EXPECT_EQ(bfi1_exp, bfi1_act);
  EXPECT_EQ(bf1, tt.bitfields_[bfi1_exp]);
}

// single transfer at stop E
inline loader::mem_dir single_transfer_data() {
  constexpr auto const fplan = R"(
*Z 01337 80____                                           %
*A VE 0000001 0000005 000001                              %
*G RE  0000001 0000005                                    %
0000001 A                            00100                %
0000005 E                     00200                       %
*Z 07331 80____                                           %
*A VE 0000005 0000009 000001                              %
*G RE  0000005 0000009                                    %
0000005 E                            00300                %
0000009 I                     00400                       %
)";
  return test_data::hrd_timetable::base().add(
      {loader::hrd::hrd_5_20_26.fplan_ / "services.101", fplan});
}

TEST(initial_transfer_computation, single_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = hrd_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, loader::hrd::hrd_5_20_26, single_transfer_data(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.initial_transfer_computation();

  ASSERT_EQ(1, tbp.ts_.transfers_.size());
  auto const transfers =
      tbp.ts_.get_transfers(transport_idx_t{0U}, location_idx_t{18U});
  ASSERT_TRUE(transfers.has_value());
  ASSERT_EQ(0U, transfers->first);
  ASSERT_EQ(1U, transfers->second);
  auto const t = tbp.ts_.transfers_[transfers->first];
  EXPECT_EQ(transport_idx_t{1U}, t.transport_idx_to_);
  EXPECT_EQ(location_idx_t{18U}, t.location_idx_to_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_from_);
  EXPECT_EQ(bitfield_idx_t{0U}, t.bitfield_idx_to_);
}

// no transfer at stop E
loader::mem_dir no_transfer_data() {
  constexpr auto const fplan = R"(
*Z 01337 80____                                           %
*A VE 0000001 0000005 000001                              %
*G RE  0000001 0000005                                    %
0000001 A                            00100                %
0000005 E                     00200                       %
*Z 07331 80____                                           %
*A VE 0000005 0000009 000001                              %
*G RE  0000005 0000009                                    %
0000005 E                            00200                %
0000009 I                     00300                       %
)";
  return test_data::hrd_timetable::base().add(
      {loader::hrd::hrd_5_20_26.fplan_ / "services.101", fplan});
}

TEST(initial_transfer_computation, no_transfer) {
  // load timetable
  timetable tt;
  tt.date_range_ = hrd_full_period();
  constexpr auto const src = source_idx_t{0U};
  load_timetable(src, loader::hrd::hrd_5_20_26, no_transfer_data(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  // run preprocessing
  tbp.initial_transfer_computation();

  ASSERT_EQ(0, tbp.ts_.transfers_.size());
}

using namespace nigiri::loader::gtfs;

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
}

// for (location_idx_t li{0U}; li != tt.n_locations(); ++li) {
//   std::cerr << "location " << li << " has footpaths to...";
//   for (auto fpi = 0U; fpi != tt.locations_.footpaths_out_[li].size(); ++fpi)
//   {
//     std::cerr << "location " << tt.locations_.footpaths_out_[li][fpi] << " ";
//   }
//   std::cerr << std::endl;
// }
