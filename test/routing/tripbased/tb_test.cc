#include "doctest/doctest.h"

#include "nigiri/types.h"
#include "nigiri/routing/tripbased/tb.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST_CASE("num_midnights") {
  duration_t zero{0U};
  CHECK_EQ(0, num_midnights(zero));

  duration_t half_day{720U};
  CHECK_EQ(0, num_midnights(half_day));

  duration_t one_day{1440U};
  CHECK_EQ(1, num_midnights(one_day));

  duration_t one_half_day{1440U + 720U};
  CHECK_EQ(1, num_midnights(one_half_day));
}