#include "gtest/gtest.h"

#include "nigiri/routing/tripbased/tb.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST(tripbased, transfer_set_basic) {

  // fill
  hash_transfer_set ts;
  std::uint64_t transport_to = 30000U;
  std::uint64_t location_to = 40000U;
  std::uint64_t bitfield_from = 50000U;
  std::uint64_t bitfield_to = 60000U;
  for (std::uint64_t transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t transport_idx_from{transport};
    for (std::uint64_t location = 20000U; location < 20010U; ++location) {
      location_idx_t location_idx_from{location};
      for (int i = 0; i < 10; ++i) {
        transport_idx_t transport_idx_to{transport_to++};
        location_idx_t location_idx_to{location_to++};
        bitfield_idx_t bitfield_idx_from{bitfield_from++};
        bitfield_idx_t bitfield_idx_to{bitfield_to++};
        ts.add(transport_idx_from, location_idx_from, transport_idx_to,
               location_idx_to, bitfield_idx_from, bitfield_idx_to);
      }
    }
  }
  ts.finalize();

  // check
  EXPECT_EQ(1000, ts.transfers_.size());
  transport_to = 30000U;
  location_to = 40000U;
  bitfield_from = 50000U;
  bitfield_to = 60000U;
  for (std::uint64_t transport = 10000U; transport < 10010U; ++transport) {
    transport_idx_t transport_idx_from{transport};
    for (std::uint64_t location = 20000U; location < 20010U; ++location) {
      location_idx_t location_idx_from{location};

      std::optional<hash_transfer_set::entry> e =
          ts.get_transfers(transport_idx_from, location_idx_from);
      ASSERT_TRUE(e.has_value());
      EXPECT_EQ(10, e->second - e->first);

      for (std::uint32_t i = e->first; i < e->second; ++i) {
        transfer t = ts[i];
        transport_idx_t transport_idx_to{transport_to++};
        location_idx_t location_idx_to{location_to++};
        bitfield_idx_t bitfield_idx_from{bitfield_from++};
        bitfield_idx_t bitfield_idx_to{bitfield_to++};
        EXPECT_EQ(transport_idx_to, t.transport_idx_to_);
        EXPECT_EQ(location_idx_to, t.location_idx_to_);
        EXPECT_EQ(bitfield_idx_from, t.bitfield_idx_from_);
        EXPECT_EQ(bitfield_idx_to, t.bitfield_idx_to_);
      }
    }
  }

  // check non-existent key
  std::optional<hash_transfer_set::entry> e =
      ts.get_transfers(transport_idx_t{23U}, location_idx_t{42U});
  EXPECT_FALSE(e.has_value());
}