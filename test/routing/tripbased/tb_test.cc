#include "gtest/gtest.h"

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST(tripbased, num_midnights) {
  duration_t const zero{0U};
  EXPECT_EQ(day_idx_t{0}, num_midnights(zero));

  duration_t const half_day{720U};
  EXPECT_EQ(day_idx_t{0}, num_midnights(half_day));

  duration_t const one_day{1440U};
  EXPECT_EQ(day_idx_t{1}, num_midnights(one_day));

  duration_t const one_half_day{1440U + 720U};
  EXPECT_EQ(day_idx_t{1}, num_midnights(one_half_day));
}

TEST(transport_segment, basic) {
  day_idx_t const base{QUERY_DAY_SHIFT};
  transport_idx_t const transport_idx{0};

  for (day_idx_t transport_day{0U}; transport_day != day_idx_t{8U};
       ++transport_day) {
    transport_segment const tps0{
        embed_day_offset(base, transport_day, transport_idx), 2U, 3U, 4U};
    EXPECT_EQ(transport_day, tps0.get_transport_day(base));
    EXPECT_EQ(transport_idx, tps0.get_transport_idx());
  }
}

TEST(hash_transfer_set, basic) {

  // fill
  hash_transfer_set ts;
  std::uint32_t transport_to = 30000U;
  std::uint32_t stop_idx_to = 0U;
  std::uint32_t bitfield_from = 50000U;
  std::uint32_t passes_midnight = 0U;
  for (std::uint64_t transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t const transport_idx_from{transport};
    for (unsigned stop_idx_from = 0U; stop_idx_from < 10U; ++stop_idx_from) {
      for (int i = 0; i < 10; ++i) {
        transport_idx_t const transport_idx_to{transport_to++};
        bitfield_idx_t const bitfield_idx_from{bitfield_from++};
        ts.add(transport_idx_from, stop_idx_from, transport_idx_to,
               stop_idx_to++, bitfield_idx_from,
               day_idx_t{passes_midnight++ % 2});
      }
    }
  }
  ts.finalize();

  // check
  EXPECT_EQ(1000, ts.transfers_.size());
  transport_to = 30000U;
  stop_idx_to = 0U;
  bitfield_from = 50000U;
  passes_midnight = 0U;
  for (unsigned transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t const transport_idx_from{transport};
    for (unsigned stop_idx_from = 0U; stop_idx_from < 10U; ++stop_idx_from) {
      std::optional<hash_transfer_set::entry> e =
          ts.get_transfers(transport_idx_from, stop_idx_from);
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