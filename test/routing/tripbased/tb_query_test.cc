#include <random>

#include "nigiri/routing/tripbased/tb_query.h"
#include "gtest/gtest.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

TEST(r_, basic) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  // update empty
  transport_idx_t const tpi0{22U};
  unsigned const si0{42U};
  bitfield const bf_010{"010"};
  tbq.r_update(tpi0, si0, bf_010);
  ASSERT_EQ(1, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);

  // update end
  transport_idx_t const tpi1{24U};
  unsigned const si1{41U};
  bitfield const bf_110{"110"};
  tbq.r_update(tpi1, si1, bf_110);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  // update inner
  transport_idx_t const tpi2{23U};
  unsigned const si2{11U};
  bitfield const bf_001{"001"};
  tbq.r_update(tpi2, si2, bf_001);
  ASSERT_EQ(3, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi2, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si2, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_001, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[2].bitfield_idx_]);

  // update existing inner, no remove
  transport_idx_t const tpi3{23U};
  unsigned const si3{12U};
  bitfield const bf_100{"100"};
  tbq.r_update(tpi3, si3, bf_100);
  ASSERT_EQ(4, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi2, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si2, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_001, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi3, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si3, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_100, tt.bitfields_[tbq.r_[2].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[3].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[3].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[3].bitfield_idx_]);

  // update existing inner, remove
  transport_idx_t const tpi4{23U};
  unsigned const si4{10U};
  bitfield const bf_111{"111"};
  tbq.r_update(tpi4, si4, bf_111);
  ASSERT_EQ(3, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf_010, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi4, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si4, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf_111, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[2].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[2].stop_idx_);
  EXPECT_EQ(bf_110, tt.bitfields_[tbq.r_[2].bitfield_idx_]);

  // r_query
  EXPECT_EQ(si4, tbq.r_query(tpi4, bf_010));
  EXPECT_EQ(si0, tbq.r_query(tpi0, bf_010));
  EXPECT_EQ(si1, tbq.r_query(tpi1, bf_100));
}

TEST(r_update, same_stop_more_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"1"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"11"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf2, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, same_stop_less_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"11"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"1"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, same_stop_other_bits) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  transport_idx_t const tpi0{0U};
  unsigned const si0{0U};
  bitfield const bf0{"0"};
  transport_idx_t const tpi1{2U};
  unsigned const si1{2U};
  bitfield const bf1{"01"};
  tbq.r_update(tpi1, si1, bf1);
  tbq.r_update(tpi0, si0, bf0);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  EXPECT_EQ(bf1, tt.bitfields_[tbq.r_[1].bitfield_idx_]);

  transport_idx_t tpi2{2U};
  unsigned const si2{2U};
  bitfield const bf2{"10"};
  tbq.r_update(tpi2, si2, bf2);
  ASSERT_EQ(2, tbq.r_.size());
  EXPECT_EQ(tpi0, tbq.r_[0].transport_idx_);
  EXPECT_EQ(si0, tbq.r_[0].stop_idx_);
  EXPECT_EQ(bf0, tt.bitfields_[tbq.r_[0].bitfield_idx_]);
  EXPECT_EQ(tpi1, tbq.r_[1].transport_idx_);
  EXPECT_EQ(si1, tbq.r_[1].stop_idx_);
  bitfield const bf3{"11"};
  EXPECT_EQ(bf3, tt.bitfields_[tbq.r_[1].bitfield_idx_]);
}

TEST(r_update, random) {
  // init
  timetable tt;
  tb_preprocessing tbp{tt};
  tb_query tbq{tbp};

  // fill params
  auto const num_updates = 100000U;
  auto const tpi_max = 100U;
  auto const si_max = 23U;
  auto const num_bits = 365U;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> tpi_dist(0, tpi_max);
  std::uniform_int_distribution<> si_dist(0, si_max);
  std::uniform_int_distribution<> bit_dist(0, 1);

  // fill
  for (auto i = 0U; i < num_updates; ++i) {
    bitfield bf;
    for (auto j = 0U; j < num_bits; ++j) {
      bf.set(j, bit_dist(gen));
    }
    tbq.r_update(transport_idx_t{tpi_dist(gen)},
                 static_cast<unsigned>(si_dist(gen)), bf);
  }

  // check sorted by transport
  for (auto it = tbq.r_.begin(); it + 1 != tbq.r_.end(); ++it) {
    EXPECT_TRUE(it->transport_idx_ <= (it + 1)->transport_idx_);
  }

  // check each stop is unique per transport
  ASSERT_TRUE(0 < tbq.r_.size());
  transport_idx_t tpi_cur = tbq.r_[0].transport_idx_;
  std::set<unsigned> si_set;
  for (auto it = tbq.r_.begin(); it != tbq.r_.end(); ++it) {
    if (it->transport_idx_ != tpi_cur) {
      tpi_cur = it->transport_idx_;
      si_set.clear();
    }
    EXPECT_EQ(si_set.end(), si_set.find(it->stop_idx_));
    si_set.emplace(it->stop_idx_);
  }

  // check bitsets are disjoint per transport
  tpi_cur = tbq.r_[0].transport_idx_;
  auto bf_or = tt.bitfields_[tbq.r_[0].bitfield_idx_];
  for (auto it = tbq.r_.begin() + 1; it != tbq.r_.end(); ++it) {
    if (it->transport_idx_ != tpi_cur) {
      tpi_cur = it->transport_idx_;
      bf_or = tt.bitfields_[it->bitfield_idx_];
    } else {
      EXPECT_TRUE((bf_or & tt.bitfields_[it->bitfield_idx_]).none());
      bf_or |= tt.bitfields_[it->bitfield_idx_];
    }
  }
}