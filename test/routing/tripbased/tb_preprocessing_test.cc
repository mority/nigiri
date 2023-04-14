#include "doctest/doctest.h"

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/routing/tripbased/tb_preprocessing.h"

#include "nigiri/print_transport.h"


#include "../../loader/hrd/hrd_timetable.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace nigiri::test_data::hrd_timetable;

std::set<std::string> service_strings2(timetable const& tt) {
  auto const reverse = [](std::string s) {
    std::reverse(s.begin(), s.end());
    return s;
  };
  auto const num_days = static_cast<size_t>(
      (tt.date_range_.to_ - tt.date_range_.from_ + 1_days) / 1_days);
  auto ret = std::set<std::string>{};
  for (auto i = 0U; i != tt.transport_traffic_days_.size(); ++i) {
    std::stringstream out;
    auto const transport_idx = transport_idx_t{i};
    auto const traffic_days =
        tt.bitfields_.at(tt.transport_traffic_days_.at(transport_idx));
    out << "TRAFFIC_DAYS="
        << reverse(
               traffic_days.to_string().substr(traffic_days.size() - num_days))
        << "\n";
    for (auto d = tt.date_range_.from_; d != tt.date_range_.to_;
         d += std::chrono::days{1}) {
      auto const day_idx = day_idx_t{
          static_cast<day_idx_t::value_t>((d - tt.date_range_.from_) / 1_days)};
      if (traffic_days.test(to_idx(day_idx))) {
        date::to_stream(out, "%F", d);
        out << " (day_idx=" << day_idx << ")\n";
        print_transport(tt, out, {transport_idx, day_idx});
      }
    }
    ret.emplace(out.str());
  }
  return ret;
}

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

  std::cerr << "num_transfers: " << tbp.ts_.transfers_.size() << std::endl;

//#ifndef NDEBUG
//  auto const ss = service_strings2(tt);
//  for(auto const& s : ss) {
//
//    std::cout << s << std::endl;
//
//  }
//#endif
}

