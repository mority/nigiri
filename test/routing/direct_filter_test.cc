#include "gtest/gtest.h"

#include "nigiri/loader/dir.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/direct_filter.h"

#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;

namespace {

// Europe/London is UTC+1 in June: S0 -> S1 departs 06:00, 07:00 and 08:00 UTC,
// S2 -> S1 departs 06:30 and 09:30 UTC, every ride takes one hour.
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
R1,DTA,R1,R1,"S2 -> S1",3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_07,R0_07,1
R0,MON,R0_08,R0_08,2
R0,MON,R0_09,R0_09,3
R1,MON,R1_0730,R1_0730,4
R1,MON,R1_1030,R1_1030,5

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_07,07:00:00,07:00:00,S0,0,0,0
R0_07,08:00:00,08:00:00,S1,1,0,0
R0_08,08:00:00,08:00:00,S0,0,0,0
R0_08,09:00:00,09:00:00,S1,1,0,0
R0_09,09:00:00,09:00:00,S0,0,0,0
R0_09,10:00:00,10:00:00,S1,1,0,0
R1_0730,07:30:00,07:30:00,S2,0,0,0
R1_0730,08:30:00,08:30:00,S1,1,0,0
R1_1030,10:30:00,10:30:00,S2,0,0,0
R1_1030,11:30:00,11:30:00,S1,1,0,0
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

// Walk 10 min to S0 or 2 min to S2, 10 min from S1 to the destination; the
// direct walk takes 15 min. Every journey via S0 walks 20 min in total and is
// not better than walking direct.
routing::query make_query(timetable const& tt,
                          routing::start_time_t const start_time,
                          bool const with_direct) {
  auto const S0 = tt.find(location_id{"S0", source_idx_t{0}}).value();
  auto const S1 = tt.find(location_id{"S1", source_idx_t{0}}).value();
  auto const S2 = tt.find(location_id{"S2", source_idx_t{0}}).value();
  auto q = routing::query{
      .start_time_ = start_time,
      .start_match_mode_ = routing::location_match_mode::kIntermodal,
      .dest_match_mode_ = routing::location_match_mode::kIntermodal,
      .use_start_footpaths_ = false,
      .start_ = {{S0, 10min, 0U}, {S2, 2min, 0U}},
      .destination_ = {{S1, 10min, 0U}},
      .min_connection_count_ = 2U,
      .extend_interval_later_ = true};
  if (with_direct) {
    q.direct_durations_ = {{routing::transport_mode_t{}, 15min}};
  }
  return q;
}

std::vector<std::pair<unixtime_t, unixtime_t>> dep_arr(
    pareto_set<routing::journey> const& journeys) {
  auto x = std::vector<std::pair<unixtime_t, unixtime_t>>{};
  for (auto const& j : journeys) {
    x.emplace_back(j.departure_time(), j.arrival_time());
  }
  return x;
}

constexpr auto kMonday = sys_days{2026_y / June / 01};

}  // namespace

TEST(routing, direct_filter_offsets) {
  auto const tt = get_tt();
  auto const S0 = tt.find(location_id{"S0", source_idx_t{0}}).value();
  auto q = make_query(tt, unixtime_t{kMonday + 5h}, true);

  auto j = routing::journey{};
  j.legs_.emplace_back(direction::kForward,
                       get_special_station(special_station::kStart), S0,
                       unixtime_t{kMonday + 5h}, unixtime_t{kMonday + 5h + 7min},
                       routing::offset{S0, 7min, 0U});
  j.legs_.emplace_back(direction::kForward, S0,
                       get_special_station(special_station::kEnd),
                       unixtime_t{kMonday + 6h}, unixtime_t{kMonday + 6h + 7min},
                       routing::offset{S0, 7min, 0U});
  EXPECT_FALSE(routing::is_not_better_than_direct(q, j));  // 14 < 15

  j.legs_.back().uses_ = routing::offset{S0, 8min, 0U};
  EXPECT_TRUE(routing::is_not_better_than_direct(q, j));  // 15 >= 15

  j.legs_.back().uses_ = routing::offset{S0, 8min, 1U};
  EXPECT_FALSE(routing::is_not_better_than_direct(q, j));  // different modes

  q.direct_durations_.clear();
  EXPECT_FALSE(routing::is_not_better_than_direct(q, j));
}

// Without direct durations Pong stops after the first two journeys. One of
// them walks longer than the direct connection and would be removed afterwards
// by the caller, which leaves one journey although two were requested.
TEST(routing, pong_direct_filter_counts_only_kept_journeys) {
  auto const tt = get_tt();

  auto const without = test::search_pong(
      tt, nullptr, make_query(tt, unixtime_t{kMonday + 5h}, false));
  EXPECT_EQ((std::vector<std::pair<unixtime_t, unixtime_t>>{
                {kMonday + 5h + 50min, kMonday + 7h + 10min},
                {kMonday + 6h + 28min, kMonday + 7h + 40min}}),
            dep_arr(without));

  auto const with = test::search_pong(
      tt, nullptr, make_query(tt, unixtime_t{kMonday + 5h}, true));
  EXPECT_EQ((std::vector<std::pair<unixtime_t, unixtime_t>>{
                {kMonday + 6h + 28min, kMonday + 7h + 40min},
                {kMonday + 9h + 28min, kMonday + 10h + 40min}}),
            dep_arr(with));
}

TEST(routing, range_query_direct_filter_counts_only_kept_journeys) {
  auto const tt = get_tt();
  auto const results = test::raptor_search(
      tt, nullptr,
      make_query(tt,
                 interval<unixtime_t>{kMonday + 5h, kMonday + 5h + 1min},
                 true));
  EXPECT_EQ((std::vector<std::pair<unixtime_t, unixtime_t>>{
                {kMonday + 6h + 28min, kMonday + 7h + 40min},
                {kMonday + 9h + 28min, kMonday + 10h + 40min}}),
            dep_arr(results));
}
