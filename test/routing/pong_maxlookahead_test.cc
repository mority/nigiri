#include "gtest/gtest.h"

#include "nigiri/loader/dir.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/raptor/pong.h"
#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;

namespace {

// S0 -> S1 on Monday (2026-06-01) at 07:00 and on Wednesday (2026-06-03) at
// 05:00 local time (= 06:00 / 04:00 UTC). A query on Monday after 09:00 UTC
// therefore only finds a journey two days later.
constexpr auto kTimetable = R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20260601,20260607
WED,0,0,1,0,0,0,0,20260601,20260607

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S0 -> S1",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,WED,R1_WED,R1_WED,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,07:00:00,07:00:00,S0,0,0,0
R0_MON,08:00:00,08:00:00,S1,1,0,0
R1_WED,05:00:00,05:00:00,S0,0,0,0
R1_WED,06:00:00,06:00:00,S1,1,0,0
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
                          interval<unixtime_t> const start_time,
                          std::optional<duration_t> const max_lookahead,
                          bool const fwd) {
  auto const S0 = tt.find(location_id{"S0", source_idx_t{0}}).value();
  auto const S1 = tt.find(location_id{"S1", source_idx_t{0}}).value();
  return routing::query{
      .start_time_ = start_time,
      .start_match_mode_ = routing::location_match_mode::kEquivalent,
      .dest_match_mode_ = routing::location_match_mode::kEquivalent,
      .use_start_footpaths_ = false,
      .start_ = {{fwd ? S0 : S1, 0min, 0U}},
      .destination_ = {{fwd ? S1 : S0, 0min, 0U}},
      .max_lookahead_ = max_lookahead,
      .min_connection_count_ = 1U,
      .extend_interval_earlier_ = !fwd,
      .extend_interval_later_ = fwd};
}

routing::routing_result run(timetable const& tt,
                            routing::search_state& s_state,
                            routing::raptor_state& r_state,
                            routing::query q,
                            direction const dir) {
  return routing::pong_search(tt, nullptr, s_state, r_state, std::move(q), dir);
}

}  // namespace

TEST(routing, pong_max_lookahead_no_limit) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};

  auto const result =
      run(tt, s_state, r_state,
          make_query(tt,
                     {unixtime_t{sys_days{2026_y / June / 01}} + 9h,
                      unixtime_t{sys_days{2026_y / June / 01}} + 10h},
                     std::nullopt, true),
          direction::kForward);

  ASSERT_EQ(1U, result.journeys_->size());
  EXPECT_EQ(unixtime_t{sys_days{2026_y / June / 03}} + 4h,
            result.journeys_->begin()->departure_time());
  EXPECT_FALSE(result.max_lookahead_exceeded_);
}

TEST(routing, pong_max_lookahead_exceeded) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};

  auto const result =
      run(tt, s_state, r_state,
          make_query(tt,
                     {unixtime_t{sys_days{2026_y / June / 01}} + 9h,
                      unixtime_t{sys_days{2026_y / June / 01}} + 10h},
                     1_days, true),
          direction::kForward);

  EXPECT_EQ(0U, result.journeys_->size());
  EXPECT_TRUE(result.max_lookahead_exceeded_);
}

TEST(routing, pong_max_lookahead_within_window) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};

  auto const result =
      run(tt, s_state, r_state,
          make_query(tt,
                     {unixtime_t{sys_days{2026_y / June / 01}},
                      unixtime_t{sys_days{2026_y / June / 01}} + 1h},
                     1_days, true),
          direction::kForward);

  ASSERT_EQ(1U, result.journeys_->size());
  EXPECT_EQ(unixtime_t{sys_days{2026_y / June / 01}} + 6h,
            result.journeys_->begin()->departure_time());
  EXPECT_FALSE(result.max_lookahead_exceeded_);
}

TEST(routing, pong_max_lookahead_backward) {
  auto const tt = get_tt();
  auto s_state = routing::search_state{};
  auto r_state = routing::raptor_state{};

  auto const start_time =
      interval<unixtime_t>{unixtime_t{sys_days{2026_y / June / 05}},
                           unixtime_t{sys_days{2026_y / June / 05}} + 1h};

  auto const unlimited =
      run(tt, s_state, r_state, make_query(tt, start_time, std::nullopt, false),
          direction::kBackward);
  ASSERT_EQ(1U, unlimited.journeys_->size());
  EXPECT_EQ(unixtime_t{sys_days{2026_y / June / 03}} + 5h,
            unlimited.journeys_->begin()->arrival_time());
  EXPECT_FALSE(unlimited.max_lookahead_exceeded_);

  auto const limited =
      run(tt, s_state, r_state, make_query(tt, start_time, 1_days, false),
          direction::kBackward);
  EXPECT_EQ(0U, limited.journeys_->size());
  EXPECT_TRUE(limited.max_lookahead_exceeded_);
}
