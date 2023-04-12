#include "doctest/doctest.h"

#include "nigiri/timetable.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST_CASE("get_or_create_bfi") {
  // init
  timetable tt{};
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