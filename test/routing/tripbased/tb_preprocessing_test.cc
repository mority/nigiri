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
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);

  // init preprocessing
  tb_preprocessing tbp{tt};

  tbp.initial_transfer_computation();

  std::cerr << "num_transfers: " << tbp.ts_.transfers_.size() << std::endl;

  for(transport_idx_t tpi_from{0U}; tpi_from < tt.transport_traffic_days_.size(); ++tpi_from) {
    route_idx_t const ri_from = tt.transport_route_[tpi_from];
    auto const stops_from = tt.route_location_seq_[ri_from];
    for(std::size_t si_from = 0U; si_from != stops_from.size(); ++si_from) {
      auto const stop = timetable::stop{stops_from[si_from]};
      // li_from: location index from
      location_idx_t const li_from = stop.location_idx();
      std::optional<hash_transfer_set::entry> transfers = tbp.ts_.get_transfers(tpi_from, li_from);
      if(transfers.has_value()) {
        for(auto i = transfers.value().first; i != transfers.value().second; ++i) {
          transfer t = tbp.ts_.transfers_[i];
          std::cerr << "tpi_from = " << tpi_from << ", li_from = " << li_from << "->" << "tpi_to = " << t.transport_idx_to_ << ", li_to" << t.location_idx_to_ << std::endl;
        }

      }
    }
  }
}