#include "gtest/gtest.h"

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST(tripbased, num_midnights) {
  duration_t zero{0U};
  EXPECT_EQ(day_idx_t{0}, num_midnights(zero));

  duration_t half_day{720U};
  EXPECT_EQ(day_idx_t{0}, num_midnights(half_day));

  duration_t one_day{1440U};
  EXPECT_EQ(day_idx_t{1}, num_midnights(one_day));

  duration_t one_half_day{1440U + 720U};
  EXPECT_EQ(day_idx_t{1}, num_midnights(one_half_day));
}

TEST(hash_transfer_set, basic) {

  // fill
  hash_transfer_set ts;
  std::uint32_t transport_to = 30000U;
  std::uint32_t stop_idx_to = 40000U;
  std::uint32_t bitfield_from = 50000U;
  std::uint32_t passes_midnight = 0U;
  for (std::uint64_t transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t const transport_idx_from{transport};
    for (unsigned stop_idx_from = 20000U; stop_idx_from < 20010U;
         ++stop_idx_from) {
      for (int i = 0; i < 10; ++i) {
        transport_idx_t const transport_idx_to{transport_to++};
        bitfield_idx_t const bitfield_idx_from{bitfield_from++};
        ts.add(transport_idx_from, stop_idx_from, transport_idx_to,
               stop_idx_to++, bitfield_idx_from, passes_midnight++ % 2);
      }
    }
  }
  ts.finalize();

  // check
  EXPECT_EQ(1000, ts.transfers_.size());
  transport_to = 30000U;
  stop_idx_to = 40000U;
  bitfield_from = 50000U;
  passes_midnight = 0U;
  for (unsigned transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t const transport_idx_from{transport};
    for (unsigned location = 20000U; location < 20010U; ++location) {
      std::optional<hash_transfer_set::entry> e =
          ts.get_transfers(transport_idx_from, location);
      ASSERT_TRUE(e.has_value());
      EXPECT_EQ(10, e->second - e->first);

      for (std::uint32_t i = e->first; i < e->second; ++i) {
        transfer const t = ts[i];
        transport_idx_t const transport_idx_to{transport_to++};
        bitfield_idx_t const bitfield_idx_from{bitfield_from++};
        EXPECT_EQ(transport_idx_to, t.transport_idx_to_);
        EXPECT_EQ(stop_idx_to++, t.stop_idx_to_);
        EXPECT_EQ(bitfield_idx_from, t.bitfield_idx_);
        EXPECT_EQ(passes_midnight++ % 2, t.passes_midnight_);
      }
    }
  }

  // check non-existent key
  std::optional<hash_transfer_set::entry> const e =
      ts.get_transfers(transport_idx_t{23U}, 42U);
  EXPECT_FALSE(e.has_value());
}