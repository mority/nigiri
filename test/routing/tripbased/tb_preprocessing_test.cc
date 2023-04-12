#include "doctest/doctest.h"

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

#include "../../loader/hrd/hrd_timetable.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace nigiri::test_data::hrd_timetable;

TEST_CASE("get_or_create_bfi") {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};

  // bitfield already registered with timetable
  bitfield bf0{"0"};
  tt.register_bitfield(bf0);
  bitfield_idx_t bfi0_exp{0U};
  CHECK_EQ(tt.bitfields_[bfi0_exp], bf0);
  tbp.bitfield_to_bitfield_idx_.emplace(bf0, bfi0_exp);
  bitfield_idx_t bfi0_act = tbp.get_or_create_bfi(bf0);
  CHECK_EQ(bfi0_exp, bfi0_act);

  // bitfield not yet registered with timetable
  bitfield bf1{"1"};
  bitfield_idx_t bfi1_exp{1U};
  bitfield_idx_t bfi1_act = tbp.get_or_create_bfi(bf1);
  CHECK_EQ(bfi1_exp, bfi1_act);
  CHECK_EQ(bf1, tt.bitfields_[bfi1_exp]);
}

TEST_CASE("initial_transfer_computation") {
  // load timetable
  timetable tt;
  auto const src = source_idx_t{0U};
  load_timetable(src, loader::hrd::hrd_5_20_26, files_simple(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  tbp.initial_transfer_computation();
}