#include "gtest/gtest.h"

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST(tripbased, num_midnights) {
  duration_t zero{0U};
  EXPECT_EQ(0, num_midnights(zero));

  duration_t half_day{720U};
  EXPECT_EQ(0, num_midnights(half_day));

  duration_t one_day{1440U};
  EXPECT_EQ(1, num_midnights(one_day));

  duration_t one_half_day{1440U + 720U};
  EXPECT_EQ(1, num_midnights(one_half_day));
}