#include "gtest/gtest.h"

#include "nigiri/loader/dir.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/search.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;

namespace {

// S0 -> S1 -> S2 needs one transfer: at least two RAPTOR rounds per search.
constexpr auto kTimetable = R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20260601,20260607

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",3
R1,DTA,R1,R1,"S1 -> S2",3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_07,R0_07,1
R0,MON,R0_09,R0_09,2
R1,MON,R1_08,R1_08,3
R1,MON,R1_10,R1_10,4

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_07,07:00:00,07:00:00,S0,0,0,0
R0_07,07:30:00,07:30:00,S1,1,0,0
R0_09,09:00:00,09:00:00,S0,0,0,0
R0_09,09:30:00,09:30:00,S1,1,0,0
R1_08,08:00:00,08:00:00,S1,0,0,0
R1_08,08:30:00,08:30:00,S2,1,0,0
R1_10,10:00:00,10:00:00,S1,0,0,0
R1_10,10:30:00,10:30:00,S2,1,0,0
)";

timetable get_tt() {
  static auto const files = mem_dir::read(kTimetable);
  timetable tt;
  tt.date_range_ = {sys_days{2026_y / June / 01}, sys_days{2026_y / June / 07}};
  register_special_stations(tt);
  load_timetable({}, source_idx_t{0}, files, tt);
  finalize(tt);
  return tt;
}

routing::query make_query(timetable const& tt,
                          routing::start_time_t const start_time) {
  auto const S0 = tt.find(location_id{"S0", source_idx_t{0}}).value();
  auto const S2 = tt.find(location_id{"S2", source_idx_t{0}}).value();
  return routing::query{.start_time_ = start_time,
                        .start_match_mode_ = routing::location_match_mode::kEquivalent,
                        .dest_match_mode_ = routing::location_match_mode::kEquivalent,
                        .start_ = {{S0, 0min, 0U}},
                        .destination_ = {{S2, 0min, 0U}},
                        .min_connection_count_ = 2U,
                        .extend_interval_later_ = true};
}

void expect_rounds(routing::routing_result const& r) {
  ASSERT_EQ(2U, r.journeys_->size());
  ASSERT_TRUE(r.algo_stats_.contains("n_rounds"));
  auto const rounds = r.algo_stats_.at("n_rounds");
  auto const searches =
      r.search_stats_.n_execute_fwd_ + r.search_stats_.n_execute_bwd_;
  EXPECT_GE(rounds, 2U * searches);  // every search needs the transfer
  EXPECT_LE(rounds, searches * (routing::kMaxTransfers + 1U));
}

}  // namespace

TEST(routing, raptor_stats_rounds_range_query) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};
  auto const monday = sys_days{2026_y / June / 01};
  auto const r = routing::raptor_search(
      tt, nullptr, s_state, r_state,
      make_query(tt, interval<unixtime_t>{unixtime_t{monday + 5h},
                                          unixtime_t{monday + 9h}}),
      direction::kForward);
  expect_rounds(r);
}

TEST(routing, raptor_stats_rounds_pong) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};
  auto const monday = sys_days{2026_y / June / 01};
  auto const r = routing::pong_search(
      tt, nullptr, s_state, r_state,
      make_query(tt, unixtime_t{monday + 5h}), direction::kForward);
  expect_rounds(r);
}

namespace {

routing::routing_result pong_window(timetable const& tt,
                                    routing::search_state& s_state,
                                    routing::raptor_state& r_state,
                                    routing::start_time_t const start_time,
                                    unsigned const min_connection_count) {
  auto q = make_query(tt, start_time);
  q.min_connection_count_ = min_connection_count;
  return routing::pong_search(tt, nullptr, s_state, r_state, std::move(q),
                              direction::kForward);
}

}  // namespace

// Journeys S0 -> S2 depart 06:00 and 08:00 UTC on Monday 2026-06-01.
TEST(routing, pong_proof_iterations) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};
  auto const t = [](auto const d) {
    return unixtime_t{sys_days{2026_y / June / 01}} + d;
  };

  // (a) window end between the two departures: the second iteration only
  // finds the 08:00 journey after the window end -> one proof iteration
  auto const a = pong_window(tt, s_state, r_state,
                             interval<unixtime_t>{t(5h), t(7h + 30min)}, 0U);
  EXPECT_EQ(1U, a.search_stats_.n_proof_iterations_);

  // (b) window end aligned with the first journey (+1 min, as the oracle):
  // nothing left to prove
  auto const b = pong_window(tt, s_state, r_state,
                             interval<unixtime_t>{t(5h), t(6h + 1min)}, 0U);
  EXPECT_EQ(0U, b.search_stats_.n_proof_iterations_);

  // (c) count-driven, empty interval: never a proof iteration
  auto const c = pong_window(tt, s_state, r_state, t(5h), 2U);
  EXPECT_EQ(2U, c.journeys_->size());
  EXPECT_EQ(0U, c.search_stats_.n_proof_iterations_);
  EXPECT_EQ(0U, c.search_stats_.proof_time_us_);

  // (d) window after the last journey of the week: the only iteration finds
  // nothing -> one proof iteration
  auto const d = pong_window(tt, s_state, r_state,
                             interval<unixtime_t>{t(9h), t(10h)}, 0U);
  EXPECT_EQ(0U, d.journeys_->size());
  EXPECT_EQ(1U, d.search_stats_.n_proof_iterations_);
}
