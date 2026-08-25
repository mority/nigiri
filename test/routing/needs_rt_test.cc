#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/needs_rt.h"
#include "nigiri/rt/create_rt_timetable.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace nigiri::routing;
using namespace std::chrono_literals;

namespace {

mem_dir test_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Europe/Berlin

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B,B,,0.02,1.03,,

# calendar_dates.txt
service_id,date,exception_type
S1,20190501,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,RE 1,,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S1,T1,RE 1,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1,10:00:00,10:00:00,A,1,0,0
T1,11:00:00,11:00:00,B,2,0,0
)");
}

constexpr auto const kDay = 2019_y / May / 1;

// `x` relative to the base day midnight UTC.
unixtime_t t(auto&& x) { return unixtime_t{sys_days{kDay} + x}; }

// Start time on `d`, 08:00 UTC. 08:00 keeps `std::chrono::round<days>()` (used
// by the interval estimator to derive the maximum interval extension) away
// from the half-day tie.
unixtime_t at_8(year_month_day const d) { return unixtime_t{sys_days{d} + 8h}; }

timetable load_tt() {
  auto tt = timetable{};
  register_special_stations(tt);
  tt.date_range_ = {date::sys_days{2019_y / March / 25},
                    date::sys_days{2019_y / November / 1}};
  load_timetable({}, source_idx_t{0}, test_files(), tt);
  finalize(tt);
  return tt;
}

query q_at(start_time_t const start,
           duration_t const max_travel_time = kMaxTravelTime) {
  auto q = query{};
  q.start_time_ = start;
  q.max_travel_time_ = max_travel_time;
  return q;
}

}  // namespace

TEST(routing, needs_rt_no_coverage) {
  auto const tt = load_tt();
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{kDay});

  ASSERT_FALSE(rtt.has_coverage());

  // Without any real-time data the query can always be answered statically -
  // regardless of where it is located in time.
  EXPECT_FALSE(needs_rt<direction::kForward>(tt, rtt, q_at(t(8h))));
  EXPECT_FALSE(needs_rt<direction::kBackward>(tt, rtt, q_at(t(8h))));
}

TEST(routing, needs_rt_overlap) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{kDay});
  rtt.extend_coverage(interval{t(8h), t(9h + 10min)});

  // Query right in the covered window.
  EXPECT_TRUE(needs_rt<direction::kForward>(tt, rtt, q_at(t(8h))));
  EXPECT_TRUE(needs_rt<direction::kBackward>(tt, rtt, q_at(t(9h))));

  // Query interval instead of a single start time.
  EXPECT_TRUE(
      needs_rt<direction::kForward>(tt, rtt, q_at(interval{t(6h), t(12h)})));
}

TEST(routing, needs_rt_far_away) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{kDay});
  rtt.extend_coverage(interval{t(8h), t(9h + 10min)});

  // A month later / earlier: even with the maximum interval extension (+-2
  // days) and the maximum travel time (5 days) the search cannot reach the
  // covered window.
  EXPECT_FALSE(
      needs_rt<direction::kForward>(tt, rtt, q_at(at_8(2019_y / June / 1))));
  EXPECT_FALSE(
      needs_rt<direction::kBackward>(tt, rtt, q_at(at_8(2019_y / June / 1))));
  EXPECT_FALSE(
      needs_rt<direction::kForward>(tt, rtt, q_at(at_8(2019_y / April / 1))));
  EXPECT_FALSE(
      needs_rt<direction::kBackward>(tt, rtt, q_at(at_8(2019_y / April / 1))));
}

// The relevant window is widened by the maximum travel time in search
// direction only - so the same query can need real-time data backwards but
// not forwards.
TEST(routing, needs_rt_direction) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{kDay});
  rtt.extend_coverage(interval{t(8h), t(9h + 10min)});

  auto const start = at_8(2019_y / May / 5);

  // Max start window is [05-03, 05-07). Forward it grows to [05-03, 05-12):
  // 05-01 is not in it.
  EXPECT_FALSE(needs_rt<direction::kForward>(tt, rtt, q_at(start)));

  // Backward it grows to [04-28, 05-07), which contains 05-01.
  EXPECT_TRUE(needs_rt<direction::kBackward>(tt, rtt, q_at(start)));

  // ... unless the journey may not be that long.
  EXPECT_FALSE(needs_rt<direction::kBackward>(tt, rtt, q_at(start, 60min)));
}

// Time dependent footpaths are not tracked by the coverage.
TEST(routing, needs_rt_profile) {
  auto const tt = load_tt();
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{kDay});

  ASSERT_FALSE(rtt.has_coverage());

  auto q = q_at(at_8(2019_y / June / 1));
  EXPECT_FALSE(needs_rt<direction::kForward>(tt, rtt, q));

  q.prf_idx_ = 1U;
  EXPECT_TRUE(needs_rt<direction::kForward>(tt, rtt, q));
}
