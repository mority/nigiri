#include "gtest/gtest.h"

#include "nigiri/routing/ablation.h"

using namespace nigiri::routing;

TEST(routing, ablation_parse) {
  EXPECT_EQ(0U, parse_ablation("").code());

  auto const a = parse_ablation("no-sharing");
  EXPECT_TRUE(a.no_sharing_);
  EXPECT_EQ(1U, a.code());

  auto const all = parse_ablation(
      "no-sharing+no-pingbound+no-skip+no-limit+no-lb+no-routelb+"
      "no-lazyreset");
  EXPECT_TRUE(all.no_sharing_ && all.no_ping_bound_ && all.no_skip_ &&
              all.no_limit_ && all.no_lb_ && all.no_route_lb_ &&
              all.no_lazy_reset_);
  EXPECT_EQ(127U, all.code());

  EXPECT_EQ(2U | 64U, parse_ablation("no-lazyreset+no-pingbound").code());
}

TEST(routing, ablation_parse_invalid) {
  EXPECT_DEATH(parse_ablation("no-foo"), "invalid NIGIRI_ABLATION");
  EXPECT_DEATH(parse_ablation("no-lb+"), "invalid NIGIRI_ABLATION");
  EXPECT_DEATH(parse_ablation("+no-lb"), "invalid NIGIRI_ABLATION");
}
