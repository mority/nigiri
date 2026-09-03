// The bidirectional lb-RAPTOR answering the same queries as
// `raptor_test` (station to station) and `raptor_intermodal_test`
// (coordinate to coordinate), on the same HRD fixture.
//
// The heuristic is not required to reproduce the pareto front in general, but
// on this fixture it has to: there is one way from A to C. What every journey
// it produces has to satisfy, always, is checked by `check_invariants` -
// connected legs, no free hop between two different locations, terminals where
// the query put them, and no two journeys riding the same vehicles.

#include <algorithm>
#include <set>
#include <variant>
#include <vector>

#include "gtest/gtest.h"

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/search.h"
#include "nigiri/special_stations.h"

#include "../loader/hrd/hrd_timetable.h"
#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;
using namespace nigiri::test_data::hrd_timetable;

namespace {

constexpr auto const src = source_idx_t{0U};

timetable load_abc() {
  auto tt = timetable{};
  tt.date_range_ = full_period();
  register_special_stations(tt);
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);
  return tt;
}

location_idx_t find_loc(timetable const& tt, std::string_view id) {
  return tt.locations_.location_id_to_idx_.at({.id_ = id, .src_ = src});
}

interval<unixtime_t> const& search_window() {
  static auto const iv =
      interval{unixtime_t{sys_days{2020_y / March / 30}} + 5_hours,
               unixtime_t{sys_days{2020_y / March / 30}} + 6_hours};
  return iv;
}

std::vector<journey> lb_search(timetable const& tt,
                               query q,
                               direction const dir = direction::kForward) {
  auto s = search_state{};
  auto const r = bidir_lb_raptor_search(tt, nullptr, s, std::move(q), dir);
  return {begin(*r.journeys_), end(*r.journeys_)};
}

bool is_transport(journey::leg const& l) {
  return std::holds_alternative<journey::run_enter_exit>(l.uses_);
}

// the vehicles a journey rides, in order - two journeys agreeing on this and
// on the transfer count are the same connection
std::vector<std::uint64_t> transports_of(journey const& j) {
  auto v = std::vector<std::uint64_t>{j.transfers_};
  for (auto const& l : j.legs_) {
    if (auto const* const ree =
            std::get_if<journey::run_enter_exit>(&l.uses_)) {
      v.push_back(to_idx(ree->r_.t_.t_idx_));
      v.push_back(to_idx(ree->r_.t_.day_));
    }
  }
  return v;
}

void check_invariants(std::vector<journey> const& js, bool const intermodal) {
  auto seen = std::set<std::vector<std::uint64_t>>{};
  for (auto const& j : js) {
    ASSERT_FALSE(j.legs_.empty());

    for (auto i = std::size_t{1U}; i != j.legs_.size(); ++i) {
      auto const& a = j.legs_[i - 1U];
      auto const& b = j.legs_[i];
      EXPECT_EQ(a.to_, b.from_) << "leg " << i - 1U << " ends at " << a.to_
                                << ", leg " << i << " starts at " << b.from_;
      EXPECT_LE(a.arr_time_, b.dep_time_) << "legs overlap in time";
    }

    // A walk that covers no time may only be a self-loop: crossing to another
    // location for free is the free-hop bug.
    for (auto const& l : j.legs_) {
      if (!is_transport(l)) {
        EXPECT_TRUE(l.from_ == l.to_ || l.dep_time_ != l.arr_time_)
            << "zero-duration walk from " << l.from_ << " to " << l.to_;
      }
    }

    auto const n_transports = static_cast<unsigned>(
        std::count_if(begin(j.legs_), end(j.legs_), is_transport));
    ASSERT_NE(0U, n_transports);
    EXPECT_EQ(n_transports - 1U, j.transfers_);

    if (intermodal) {
      EXPECT_EQ(get_special_station(special_station::kStart),
                j.legs_.front().from_);
      EXPECT_EQ(get_special_station(special_station::kEnd), j.legs_.back().to_);
    }

    EXPECT_TRUE(seen.insert(transports_of(j)).second)
        << "two journeys ride the same vehicles with the same transfer count";
  }
}

void expect_contains(std::vector<journey> const& js, journey const& r) {
  EXPECT_TRUE(std::any_of(begin(js), end(js),
                          [&](journey const& l) {
                            return l.departure_time() == r.departure_time() &&
                                   l.arrival_time() == r.arrival_time() &&
                                   l.transfers_ == r.transfers_;
                          }))
      << "lb misses dep=" << r.departure_time() << " arr=" << r.arrival_time()
      << " transfers=" << unsigned{r.transfers_};
}

}  // namespace

TEST(routing, lb_raptor_station_to_station) {
  auto const tt = load_abc();

  auto q = query{.start_time_ = search_window(),
                 .start_match_mode_ = location_match_mode::kEquivalent,
                 .dest_match_mode_ = location_match_mode::kEquivalent,
                 .use_start_footpaths_ = true,
                 .start_ = {{find_loc(tt, "0000001"), 0_minutes, 0U}},
                 .destination_ = {{find_loc(tt, "0000003"), 0_minutes, 0U}},
                 // the search window plays no part: this is what decides how
                 // many departures per pattern are realized
                 .min_connection_count_ = 5U};
  auto const js = lb_search(tt, q);

  ASSERT_FALSE(js.empty());
  check_invariants(js, /*intermodal=*/false);

  // no access/egress walk was asked for, so every journey starts and ends on a
  // transport
  for (auto const& j : js) {
    EXPECT_TRUE(is_transport(j.legs_.front()));
    EXPECT_TRUE(is_transport(j.legs_.back()));
    EXPECT_EQ(find_loc(tt, "0000001"), j.legs_.front().from_);
    EXPECT_EQ(find_loc(tt, "0000003"), j.legs_.back().to_);
  }

  for (auto const& r : nigiri::test::raptor_search(
           tt, nullptr, "0000001", "0000003", search_window())) {
    expect_contains(js, r);
  }
}

TEST(routing, lb_raptor_intermodal) {
  auto const tt = load_abc();

  auto q = query{.start_time_ = search_window(),
                 .start_match_mode_ = location_match_mode::kIntermodal,
                 .dest_match_mode_ = location_match_mode::kIntermodal,
                 .start_ = {{find_loc(tt, "0000001"), 10_minutes, 99U}},
                 .destination_ = {{find_loc(tt, "0000003"), 15_minutes, 77U}},
                 .min_connection_count_ = 5U};
  auto const js = lb_search(tt, q);

  ASSERT_FALSE(js.empty());
  check_invariants(js, /*intermodal=*/true);

  for (auto const& j : js) {
    // the mumo legs are the ones the query asked for, at their full duration
    EXPECT_EQ(10_minutes,
              j.legs_.front().arr_time_ - j.legs_.front().dep_time_);
    EXPECT_EQ(15_minutes, j.legs_.back().arr_time_ - j.legs_.back().dep_time_);
  }

  auto const ref = nigiri::test::raptor_intermodal_search(
      tt, nullptr, {{find_loc(tt, "0000001"), 10_minutes, 99U}},
      {{find_loc(tt, "0000003"), 15_minutes, 77U}}, search_window());
  for (auto const& r : ref) {
    expect_contains(js, r);
  }
}

TEST(routing, lb_raptor_station_to_station_backward) {
  auto const tt = load_abc();

  // arrive_by: the query is flipped, `start_` is the journey destination
  auto q = query{.start_time_ = search_window(),
                 .start_match_mode_ = location_match_mode::kEquivalent,
                 .dest_match_mode_ = location_match_mode::kEquivalent,
                 .use_start_footpaths_ = true,
                 .start_ = {{find_loc(tt, "0000003"), 0_minutes, 0U}},
                 .destination_ = {{find_loc(tt, "0000001"), 0_minutes, 0U}},
                 .min_connection_count_ = 5U};
  auto const js = lb_search(tt, q, direction::kBackward);

  ASSERT_FALSE(js.empty());
  check_invariants(js, /*intermodal=*/false);

  for (auto const& j : js) {
    EXPECT_EQ(find_loc(tt, "0000001"), j.legs_.front().from_);
    EXPECT_EQ(find_loc(tt, "0000003"), j.legs_.back().to_);
  }
}
