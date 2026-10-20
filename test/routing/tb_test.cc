#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/raptor/debug.h"
#include "nigiri/routing/search.h"
#include "nigiri/routing/tb/preprocess.h"
#include "nigiri/routing/tb/query_engine.h"

#include "../loader/hrd/hrd_timetable.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::routing;
using namespace nigiri::loader;
using namespace std::chrono_literals;
using namespace nigiri::test_data::hrd_timetable;

timetable load_gtfs(auto const& files) {
  timetable tt;
  tt.date_range_ = {date::sys_days{2021_y / March / 1},
                    date::sys_days{2021_y / March / 8}};
  register_special_stations(tt);
  gtfs::load_timetable({}, source_idx_t{0}, files(), tt);
  finalize(tt);
  return tt;
}
timetable load_hrd(auto const& files) {
  timetable tt;
  tt.date_range_ = full_period();
  register_special_stations(tt);
  hrd::load_timetable(source_idx_t{0U}, loader::hrd::hrd_5_20_26, files(), tt);
  finalize(tt);
  return tt;
}

pareto_set<routing::journey> tripbased_search(timetable const& tt,
                                              tb::tb_data const& tbd,
                                              routing::query q) {
  static auto search_state = routing::search_state{};
  auto algo_state = tb::query_state{tt, tbd};

  return *(routing::search<direction::kForward, tb::query_engine<false>>{
      tt, nullptr, search_state, algo_state, std::move(q)}
               .execute()
               .journeys_);
}

pareto_set<routing::journey> tripbased_search(
    timetable const& tt,
    tb::tb_data const& tbd,
    std::string_view from,
    std::string_view to,
    routing::start_time_t const time) {
  auto const src = source_idx_t{0};
  auto q = routing::query{
      .start_time_ = time,
      .start_ = {{tt.locations_.location_id_to_idx_.at({from, src}), 0_minutes,
                  0U}},
      .destination_ = {
          {tt.locations_.location_id_to_idx_.at({to, src}), 0_minutes, 0U}}};
  return tripbased_search(tt, tbd, std::move(q));
}

pareto_set<routing::journey> tripbased_intermodal_search(
    timetable const& tt,
    tb::tb_data const& tbd,
    std::vector<routing::offset> start,
    std::vector<routing::offset> destination,
    interval<unixtime_t> interval,
    std::uint8_t const min_connection_count = 0U,
    bool const extend_interval_earlier = false,
    bool const extend_interval_later = false) {
  auto q = routing::query{
      .start_time_ = interval,
      .start_match_mode_ = routing::location_match_mode::kIntermodal,
      .dest_match_mode_ = routing::location_match_mode::kIntermodal,
      .start_ = std::move(start),
      .destination_ = std::move(destination),
      .min_connection_count_ = min_connection_count,
      .extend_interval_earlier_ = extend_interval_earlier,
      .extend_interval_later_ = extend_interval_later};
  return tripbased_search(tt, tbd, std::move(q));
}

std::string results_str(auto const& results, timetable const& tt) {
  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n";
  }
  return ss.str();
}

mem_dir no_transfer_files() {
  return mem_dir::read(R"(
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
MON,1,0,0,0,0,0,0,20210301,20210307
THU,0,0,0,1,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,THU,R1_THU,R1_THU,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,12:00:00,12:00:00,S1,1,0,0
R1_THU,06:00:00,06:00:00,S1,0,0,0
R1_THU,07:00:00,07:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, no_transfer) {
  auto const tt = load_gtfs(no_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  for (auto const transfers : tbd.segment_transfers_) {
    EXPECT_TRUE(transfers.empty());
  }
}

mem_dir same_day_transfer_files() {
  return mem_dir::read(R"(
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
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,MON,R1_MON,R1_MON,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,06:00:00,06:00:00,S1,1,0,0
R1_MON,12:00:00,12:00:00,S1,0,0,0
R1_MON,13:00:00,13:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, same_day_transfer) {
  auto const tt = load_gtfs(same_day_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}];
  ASSERT_EQ(1U, tbd.segment_transfers_[s].size());
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}],
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

constexpr auto const same_day_transfer_journeys = R"(
[2021-02-28 23:00, 2021-03-01 13:00]
TRANSFERS: 1
     FROM: (S0, S0) [2021-03-01 00:00]
       TO: (S2, S2) [2021-03-01 13:00]
leg 0: (S0, S0) [2021-03-01 00:00] -> (S1, S1) [2021-03-01 06:00]
   0: S0      S0..............................................                               d: 01.03 00:00 [01.03 00:00]  [{name=R0, day=2021-03-01, id=R0_MON, src=0}]
   1: S1      S1.............................................. a: 01.03 06:00 [01.03 06:00]
leg 1: (S1, S1) [2021-03-01 06:00] -> (S1, S1) [2021-03-01 06:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 12:00] -> (S2, S2) [2021-03-01 13:00]
   0: S1      S1..............................................                               d: 01.03 12:00 [01.03 12:00]  [{name=R1, day=2021-03-01, id=R1_MON, src=0}]
   1: S2      S2.............................................. a: 01.03 13:00 [01.03 13:00]
leg 3: (S2, S2) [2021-03-01 13:00] -> (S2, S2) [2021-03-01 13:00]
  FOOTPATH (duration=0)

)";

TEST(tb_query, same_day_transfer) {
  auto const tt = load_gtfs(same_day_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_search(
      tt, tbd, "S0", "S2", unixtime_t{sys_days{February / 28 / 2021} + 23h});
  EXPECT_EQ(std::string_view{same_day_transfer_journeys},
            results_str(results, tt));
}

mem_dir next_day_transfer_files() {
  return mem_dir::read(R"(
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
MON,1,0,0,0,0,0,0,20210301,20210307
TUE,0,1,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,TUE,R1_TUE,R1_TUE,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,12:00:00,12:00:00,S1,1,0,0
R1_TUE,06:00:00,06:00:00,S1,0,0,0
R1_TUE,08:00:00,08:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, next_day_transfer) {
  auto const tt = load_gtfs(next_day_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}];
  ASSERT_TRUE(tbd.segment_transfers_[s].size() == 1U);
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}],
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(1, t.day_offset_);
}

mem_dir long_transfer_files() {
  return mem_dir::read(R"(
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
MON,1,0,0,0,0,0,0,20210301,20210307
THU,0,0,0,1,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,THU,R1_THU,R1_THU,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,76:00:00,76:00:00,S1,1,0,0
R1_THU,06:00:00,06:00:00,S1,0,0,0
R1_THU,07:00:00,07:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, long_transfer) {
  auto const tt = load_gtfs(long_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}];
  ASSERT_TRUE(tbd.segment_transfers_[s].size() == 1U);
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}],
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(3, t.day_offset_);
}

mem_dir weekday_transfer_files() {
  return mem_dir::read(R"(
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
WD,1,1,1,1,1,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,WD,R0_WD,R0_WD,1
R1,WD,R1_WD,R1_WD,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_WD,00:00:00,00:00:00,S0,0,0,0
R0_WD,02:00:00,02:00:00,S1,1,0,0
R1_WD,01:00:00,01:00:00,S1,0,0,0
R1_WD,03:00:00,03:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, weekday_transfer) {
  auto const tt = load_gtfs(weekday_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}];
  ASSERT_TRUE(tbd.segment_transfers_[s].size() == 1U);
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}],
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"0111100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(1, t.day_offset_);
}

mem_dir daily_transfer_files() {
  return mem_dir::read(R"(
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
DLY,1,1,1,1,1,1,1,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,DLY,R0_DLY,R0_DLY,1
R1,DLY,R1_DLY,R1_DLY,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_DLY,00:00:00,00:00:00,S0,0,0,0
R0_DLY,02:00:00,02:00:00,S1,1,0,0
R1_DLY,03:00:00,03:00:00,S1,0,0,0
R1_DLY,04:00:00,04:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, daily_transfer) {
  auto const tt = load_gtfs(daily_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}];
  ASSERT_TRUE(tbd.segment_transfers_[s].size() == 1U);
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}],
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"111111100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

mem_dir earlier_stop_transfer_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S4",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON0,R0_MON0,0
R0,MON,R0_MON1,R0_MON1,1

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON0,00:00:00,00:00:00,S0,0,0,0
R0_MON0,01:00:00,01:00:00,S1,1,0,0
R0_MON0,02:00:00,02:00:00,S2,2,0,0
R0_MON0,03:00:00,03:00:00,S3,3,0,0
R0_MON0,04:00:00,04:00:00,S1,4,0,0
R0_MON0,05:00:00,05:00:00,S4,5,0,0
R0_MON1,04:00:00,04:00:00,S0,0,0,0
R0_MON1,05:00:00,05:00:00,S1,1,0,0
R0_MON1,06:00:00,06:00:00,S2,2,0,0
R0_MON1,07:00:00,07:00:00,S3,3,0,0
R0_MON1,08:00:00,08:00:00,S1,4,0,0
R0_MON1,09:00:00,09:00:00,S4,5,0,0
)");
}

TEST(tb_preprocess, earlier_stop_transfer) {
  auto const tt = load_gtfs(earlier_stop_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}] + 3U;
  ASSERT_TRUE(tbd.segment_transfers_[s].size() == 1U);
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}] + 1U,
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

constexpr auto const earlier_stop_transfer_journeys = R"(
[2021-02-28 23:00, 2021-03-01 06:00]
TRANSFERS: 1
     FROM: (S3, S3) [2021-03-01 03:00]
       TO: (S2, S2) [2021-03-01 06:00]
leg 0: (S3, S3) [2021-03-01 03:00] -> (S1, S1) [2021-03-01 04:00]
   3: S3      S3..............................................                               d: 01.03 03:00 [01.03 03:00]  [{name=R0, day=2021-03-01, id=R0_MON0, src=0}]
   4: S1      S1.............................................. a: 01.03 04:00 [01.03 04:00]
leg 1: (S1, S1) [2021-03-01 04:00] -> (S1, S1) [2021-03-01 04:02]
  FOOTPATH (duration=2)
leg 2: (S1, S1) [2021-03-01 05:00] -> (S2, S2) [2021-03-01 06:00]
   1: S1      S1..............................................                               d: 01.03 05:00 [01.03 05:00]  [{name=R0, day=2021-03-01, id=R0_MON1, src=0}]
   2: S2      S2.............................................. a: 01.03 06:00 [01.03 06:00]
leg 3: (S2, S2) [2021-03-01 06:00] -> (S2, S2) [2021-03-01 06:00]
  FOOTPATH (duration=0)

)";

TEST(tb_query, early_stop_transfer) {
  auto const tt = load_gtfs(earlier_stop_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_search(
      tt, tbd, "S3", "S2", unixtime_t{sys_days{February / 28 / 2021} + 23h});
  EXPECT_EQ(std::string_view{earlier_stop_transfer_journeys},
            results_str(results, tt));
}

TEST(tb_query, no_journey_possible) {
  auto const tt = load_gtfs(earlier_stop_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_search(
      tt, tbd, "S4", "S0", unixtime_t{sys_days{February / 28 / 2021} + 23h});
  EXPECT_EQ(0, results.size());
}

mem_dir earlier_transport_transfer_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S4",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON0,R0_MON0,0
R0,MON,R0_MON1,R0_MON1,1

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON0,00:00:00,00:00:00,S0,0,0,0
R0_MON0,01:00:00,01:00:00,S1,1,0,0
R0_MON0,02:00:00,02:00:00,S2,2,0,0
R0_MON0,03:00:00,03:00:00,S3,3,0,0
R0_MON0,04:00:00,04:00:00,S1,4,0,0
R0_MON0,05:00:00,05:00:00,S4,5,0,0
R0_MON1,02:00:00,02:00:00,S0,0,0,0
R0_MON1,03:00:00,03:00:00,S1,1,0,0
R0_MON1,04:00:00,04:00:00,S2,2,0,0
R0_MON1,05:00:00,05:00:00,S3,3,0,0
R0_MON1,05:00:00,05:00:00,S1,4,0,0
R0_MON1,06:00:00,06:00:00,S4,5,0,0
)");
}

TEST(tb_preprocess, earlier_transport_transfer) {
  auto const tt = load_gtfs(earlier_transport_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{1U}];
  ASSERT_EQ(1U, tbd.segment_transfers_[s].size());
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{0U}] + 4U,
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{0U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

mem_dir uturn_transfer_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307


# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1 -> S2",2
R1,DTA,R1,R1,"S2 -> S1 -> S3",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,1

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S1,1,0,0
R0_MON,02:00:00,02:00:00,S2,2,0,0
R1_MON,03:00:00,03:00:00,S2,0,0,0
R1_MON,04:00:00,04:00:00,S1,1,0,0
R1_MON,05:00:00,05:00:00,S3,2,0,0
)");
}

TEST(tb_preprocess, uturn_transfer) {
  auto const tt = load_gtfs(uturn_transfer_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  for (auto s = tb::segment_idx_t{1U}; s < tbd.segment_transfers_.size(); ++s) {
    EXPECT_TRUE(tbd.segment_transfers_[s].empty());
  }
  ASSERT_EQ(1U, tbd.segment_transfers_[tb::segment_idx_t{0U}].size());
  auto const t = tbd.segment_transfers_[tb::segment_idx_t{0U}][0];
  EXPECT_EQ(tb::segment_idx_t{3U}, tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

mem_dir unnecessary_transfer_1_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1 -> S2",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,1

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S1,1,0,0
R0_MON,02:00:00,02:00:00,S2,2,0,0
R1_MON,01:00:00,01:10:00,S1,0,0,0
R1_MON,04:00:00,04:00:00,S2,1,0,0
)");
}

TEST(tb_preprocess, unnecessary_transfer_1) {
  auto const tt = load_gtfs(unnecessary_transfer_1_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  for (auto const transfers : tbd.segment_transfers_) {
    EXPECT_TRUE(transfers.empty());
  }
}

mem_dir unnecessary_transfer_2_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,
S5,S5,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S2 -> S3 -> S4",2
R1,DTA,R1,R1,"S1 -> S2 -> S3 -> S5",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,1

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S2,1,0,0
R0_MON,02:00:00,02:00:00,S3,2,0,0
R0_MON,03:00:00,03:00:00,S4,3,0,0
R1_MON,00:10:00,00:10:00,S1,0,0,0
R1_MON,01:10:00,01:10:00,S2,1,0,0
R1_MON,02:10:00,02:10:00,S3,2,0,0
R1_MON,03:10:00,03:10:00,S5,3,0,0
)");
}

TEST(tb_preprocess, unnecessary_transfer_2) {
  auto const tt = load_gtfs(unnecessary_transfer_2_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const s = tbd.transport_first_segment_[transport_idx_t{0U}] + 1U;
  ASSERT_EQ(1U, tbd.segment_transfers_[s].size());
  auto const t = tbd.segment_transfers_[s][0];
  EXPECT_EQ(tbd.transport_first_segment_[transport_idx_t{1U}] + 2U,
            tbd.to_segment(tt, t));
  // EXPECT_EQ(transport_idx_t{1U}, t.to_transport_);
  EXPECT_EQ(bitfield{"100000"}, tbd.bitfields_[t.traffic_days_]);
  // EXPECT_EQ(0, t.day_offset_);
}

mem_dir profile_footpath_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,49.0,8.0,,,
S1,S1,,49.1,8.0,,,
S2,S2,,49.2,8.0,,,
X,X,,49.3,8.0,,,
Y,Y,,49.3,8.001,,,

# transfers.txt
from_stop_id,to_stop_id,transfer_type,min_transfer_time
X,Y,2,300
Y,X,2,300

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1 -> S2",2
R1,DTA,R1,R1,"S2 -> X",2
R2,DTA,R2,R2,"S1 -> Y",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,1
R2,MON,R2_MON,R2_MON,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S1,1,0,0
R0_MON,02:00:00,02:00:00,S2,2,0,0
R1_MON,02:10:00,02:10:00,S2,0,0,0
R1_MON,03:00:00,03:00:00,X,1,0,0
R2_MON,01:10:00,01:10:00,S1,0,0,0
R2_MON,03:30:00,03:30:00,Y,1,0,0
)");
}

// Profile 1 walks X <-> Y in 60 min instead of 5 min. Then reaching Y via
// R1 + walk (04:00) is slower than R2 (03:30), so the transfer R0 -> R2 at S1
// is needed. The reduction has to use the footpaths of the profile it
// preprocesses for.
TEST(tb_preprocess, profile_footpaths) {
  auto tt = load_gtfs(profile_footpath_files);
  auto const loc = [&](std::string_view id) {
    return tt.locations_.location_id_to_idx_.at({id, source_idx_t{0}});
  };
  auto const x = loc("X");
  auto const y = loc("Y");

  auto const slow = profile_idx_t{1U};
  for (auto const dir :
       {&tt.locations_.footpaths_out_, &tt.locations_.footpaths_in_}) {
    auto& fps = *dir;
    for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
      auto v = std::vector<footpath>{};
      for (auto const fp : fps[kDefaultProfile][l]) {
        auto const xy =
            (l == x && fp.target() == y) || (l == y && fp.target() == x);
        v.emplace_back(fp.target(), xy ? 60_minutes : fp.duration());
      }
      fps[slow].emplace_back(v);
    }
  }
  ASSERT_EQ(1U, tt.locations_.footpaths_out_[kDefaultProfile][x].size());
  ASSERT_LT(tt.locations_.footpaths_out_[kDefaultProfile][x][0].duration(),
            30_minutes);

  auto const first_stop_route = [&](std::string_view id) {
    for (auto r = route_idx_t{0U}; r != tt.n_routes(); ++r) {
      if (stop{tt.route_location_seq_[r][0]}.location_idx() == loc(id)) {
        return r;
      }
    }
    throw std::runtime_error{"route not found"};
  };
  auto const r0 = first_stop_route("S0");
  auto const r1 = first_stop_route("S2");
  auto const r2 = first_stop_route("S1");
  auto const t0 = tt.route_transport_ranges_[r0].from_;

  auto const routes_at = [&](tb::tb_data const& tbd, stop_idx_t const i) {
    auto routes = std::vector<route_idx_t>{};
    for (auto const t :
         tbd.segment_transfers_[tbd.transport_first_segment_[t0] + i - 1U]) {
      routes.push_back(t.get_route());
    }
    return routes;
  };

  auto const fast = tb::preprocess(tt, kDefaultProfile);
  EXPECT_EQ(std::vector<route_idx_t>{}, routes_at(fast, 1U));
  EXPECT_EQ(std::vector<route_idx_t>{r1}, routes_at(fast, 2U));

  auto const slow_tbd = tb::preprocess(tt, slow);
  EXPECT_EQ(std::vector<route_idx_t>{r2}, routes_at(slow_tbd, 1U));
  EXPECT_EQ(std::vector<route_idx_t>{r1}, routes_at(slow_tbd, 2U));
}

// T: SA 09:00 -> SB 10:00 -> SE 10:05; U: SB 10:10 -> SC 10:30; P: SE 10:10 ->
// SC 10:20 (no drop-off) -> SD 10:40. The transfer reduction processes T's
// transfer to P at SE first; P passes SC earlier than U, but nobody can get off
// there, so T's transfer to U at SB is needed (SA -> SC).
mem_dir no_alighting_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
SA,SA,,,,,,
SB,SB,,,,,,
SC,SC,,,,,,
SD,SD,,,,,,
SE,SE,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
RT,DTA,RT,RT,"SA -> SE",3
RP,DTA,RP,RP,"SE -> SD",3
RU,DTA,RU,RU,"SB -> SC",3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
RT,MON,T,T,
RP,MON,P,P,
RU,MON,U,U,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T,09:00:00,09:00:00,SA,0,0,0
T,10:00:00,10:00:00,SB,1,0,0
T,10:05:00,10:05:00,SE,2,0,0
P,10:10:00,10:10:00,SE,0,0,0
P,10:20:00,10:20:00,SC,1,0,1
P,10:40:00,10:40:00,SD,2,0,0
U,10:10:00,10:10:00,SB,0,0,0
U,10:30:00,10:30:00,SC,1,0,0
)");
}

TEST(tb_preprocess, no_alighting_transfer) {
  auto const tt = load_gtfs(no_alighting_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const first_stop = [&](transport_idx_t const t) {
    auto const l = stop{tt.route_location_seq_[tt.transport_route_[t]].front()}
                       .location_idx();
    return std::string{tt.get_default_name(l)};
  };
  auto t = transport_idx_t::invalid(), u = transport_idx_t::invalid();
  for (auto x = transport_idx_t{0U}; x != tt.transport_traffic_days_.size();
       ++x) {
    if (first_stop(x) == "SA") {
      t = x;
    } else if (first_stop(x) == "SB") {
      u = x;
    }
  }
  ASSERT_NE(transport_idx_t::invalid(), t);
  ASSERT_NE(transport_idx_t::invalid(), u);
  auto const s = tbd.transport_first_segment_[t];  // SA -> SB
  ASSERT_EQ(1U, tbd.segment_transfers_[s].size());
  EXPECT_EQ(tt.transport_route_[u], tbd.segment_transfers_[s][0].get_route());
}

// X and Y (SA -> SB -> SC) both depart SA at 10:00. X is faster (SB 10:04,
// SC 10:10; Y: SB 10:07, SC 10:20) and precedes Y in their route (Y is
// loaded first). F (S0 -> SB -> SD -> SB) reaches X at its first visit of SB
// (09:58 + 2 min transfer time) and only Y at its second (10:04 + 2 min).
// Line-based pruning processes the second visit first; ordering the reached
// transports by their departure at SA ranked X like Y, so Y dominated X and
// the transfer to X was dropped (S0 -> SC: 10:20 instead of 10:10).
mem_dir equal_start_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
SA,SA,,,,,,
SB,SB,,,,,,
SC,SC,,,,,,
SD,SD,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
RF,DTA,RF,RF,"S0 -> SB",3
RR,DTA,RR,RR,"SA -> SC",3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
RR,MON,Y,Y,
RR,MON,X,X,
RF,MON,F,F,

# transfers.txt
from_stop_id,to_stop_id,transfer_type,min_transfer_time
SB,SB,2,120

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
Y,10:00:00,10:00:00,SA,0,0,0
Y,10:07:00,10:07:00,SB,1,0,0
Y,10:20:00,10:20:00,SC,2,0,0
X,10:00:00,10:00:00,SA,0,0,0
X,10:04:00,10:04:00,SB,1,0,0
X,10:10:00,10:10:00,SC,2,0,0
F,09:00:00,09:00:00,S0,0,0,0
F,09:58:00,09:58:00,SB,1,0,0
F,10:00:00,10:00:00,SD,2,0,0
F,10:04:00,10:04:00,SB,3,0,0
)");
}

TEST(tb_preprocess, equal_start_time) {
  auto const tt = load_gtfs(equal_start_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto f = transport_idx_t::invalid(), x = transport_idx_t::invalid(),
       y = transport_idx_t::invalid();
  for (auto t = transport_idx_t{0U}; t != tt.transport_traffic_days_.size();
       ++t) {
    auto const first =
        stop{tt.route_location_seq_[tt.transport_route_[t]].front()};
    if (tt.get_default_name(first.location_idx()) == "S0") {
      f = t;
    } else if (tt.event_mam(t, 1, event_type::kArr).count() == 10 * 60 + 4) {
      x = t;
    } else {
      y = t;
    }
  }
  ASSERT_NE(transport_idx_t::invalid(), f);
  ASSERT_NE(transport_idx_t::invalid(), x);
  ASSERT_NE(transport_idx_t::invalid(), y);
  ASSERT_EQ(tt.transport_route_[x], tt.transport_route_[y]);
  ASSERT_LT(x, y);

  // F's first visit of SB: the transfer to X
  auto const& transfers =
      tbd.segment_transfers_[tbd.transport_first_segment_[f]];
  auto const x_offset =
      to_idx(x - tt.route_transport_ranges_[tt.transport_route_[x]].from_);
  EXPECT_TRUE(std::any_of(
      begin(transfers), end(transfers), [&](tb::transfer const& tr) {
        return tr.get_route() == tt.transport_route_[x] &&
               tr.transport_offset_ == x_offset && tr.to_segment_offset_ == 1U;
      }));

  auto const results = tripbased_search(
      tt, tbd, "S0", "SC", unixtime_t{sys_days{March / 1 / 2021} + 8h});
  ASSERT_NE(0U, results.size());
  auto earliest = results.begin()->dest_time_;
  for (auto const& j : results) {
    earliest = std::min(earliest, j.dest_time_);
  }
  EXPECT_EQ(unixtime_t{sys_days{March / 1 / 2021} + 10h + 10min}, earliest);
}

TEST(tb_preprocess, transfer_fields) {
  // wider than the former fields (16 bits transport offset, 12 bits stop
  // index): full Transitous has 1.9 M routes, routes with up to 213 656
  // transports (frequencies.txt)
  for (auto const& [r, transport_offset, to_segment_offset, day_offset] :
       {std::tuple{route_idx_t{0U}, 0U, stop_idx_t{0U}, std::int8_t{-8}},
        std::tuple{route_idx_t{(1U << 24U) - 1U}, (1U << 20U) - 1U,
                   stop_idx_t{65'535U}, std::int8_t{7}},
        std::tuple{route_idx_t{1'900'000U}, 213'655U, stop_idx_t{4'097U},
                   std::int8_t{-1}}}) {
    auto t = tb::transfer{};
    t.set_route(r);
    t.transport_offset_ = transport_offset;
    t.to_segment_offset_ = to_segment_offset;
    t.set_day_offset(day_offset);
    EXPECT_EQ(r, t.get_route());
    EXPECT_EQ(transport_offset, t.transport_offset_);
    EXPECT_EQ(to_segment_offset, t.to_segment_offset_);
    EXPECT_EQ(day_offset, t.get_day_offset());
  }
}

mem_dir early_train_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307
THU,0,0,0,1,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S2 -> S3",2
R1,DTA,R1,R1,"S1 -> S2",2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,THU,R1_THU,R1_THU,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,76:00:00,80:00:00,S2,1,0,0
R0_MON,81:00:00,81:00:00,S3,2,0,0
R1_THU,06:00:00,06:00:00,S1,0,0,0
R1_THU,07:00:00,07:00:00,S2,1,0,0
)");
}

constexpr auto const early_train_journeys = R"(
[2021-03-04 05:00, 2021-03-04 09:00]
TRANSFERS: 1
     FROM: (S1, S1) [2021-03-04 06:00]
       TO: (S3, S3) [2021-03-04 09:00]
leg 0: (S1, S1) [2021-03-04 06:00] -> (S2, S2) [2021-03-04 07:00]
   0: S1      S1..............................................                               d: 04.03 06:00 [04.03 06:00]  [{name=R1, day=2021-03-04, id=R1_THU, src=0}]
   1: S2      S2.............................................. a: 04.03 07:00 [04.03 07:00]
leg 1: (S2, S2) [2021-03-04 07:00] -> (S2, S2) [2021-03-04 07:02]
  FOOTPATH (duration=2)
leg 2: (S2, S2) [2021-03-04 08:00] -> (S3, S3) [2021-03-04 09:00]
   1: S2      S2..............................................                               d: 04.03 08:00 [04.03 08:00]  [{name=R0, day=2021-03-01, id=R0_MON, src=0}]
   2: S3      S3.............................................. a: 04.03 09:00 [04.03 09:00]
leg 3: (S3, S3) [2021-03-04 09:00] -> (S3, S3) [2021-03-04 09:00]
  FOOTPATH (duration=0)

)";

TEST(tb_preprocess, early_train_journeys) {
  auto const tt = load_gtfs(early_train_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  tbd.print(std::cout, tt);
  ASSERT_EQ("R1", tt.transport_name(transport_idx_t{1U}));
  // auto const s = tbd.transport_first_segment_[transport_idx_t{1U}];
  // ASSERT_EQ(1U, tbd.segment_transfers_[s].size());
}

TEST(tb_query, early_train) {
  auto const tt = load_gtfs(early_train_files);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_search(
      tt, tbd, "S1", "S3", unixtime_t{sys_days{March / 04 / 2021} + 5h});
  EXPECT_EQ(std::string_view{early_train_journeys}, results_str(results, tt));
}

constexpr auto const abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)

)";

TEST(tb_query, abc) {
  auto const tt = load_hrd(files_abc);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results =
      tripbased_search(tt, tbd, "0000001", "0000003",
                       unixtime_t{sys_days{March / 30 / 2020} + 5h});
  EXPECT_EQ(std::string_view{abc_journeys}, results_str(results, tt));
}

constexpr auto const profile_abc_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]
leg 3: (C, 0000003) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 07:15]
  FOOTPATH (duration=0)

[2020-03-30 05:30, 2020-03-30 07:45]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:30]
       TO: (C, 0000003) [2020-03-30 07:45]
leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 3: (C, 0000003) [2020-03-30 07:45] -> (C, 0000003) [2020-03-30 07:45]
  FOOTPATH (duration=0)

)";

TEST(tb_query, profile_abc) {
  auto const tt = load_hrd(files_abc);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results =
      tripbased_search(tt, tbd, "0000001", "0000003",
                       interval{unixtime_t{sys_days{March / 30 / 2020}} + 5h,
                                unixtime_t{sys_days{March / 30 / 2020}} + 6h});
  EXPECT_EQ(std::string_view{profile_abc_journeys}, results_str(results, tt));
}

constexpr auto const intermodal_abc_journeys = R"(
[2020-03-30 05:20, 2020-03-30 08:00]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:20]
       TO: (END, END) [2020-03-30 08:00]
leg 0: (START, START) [2020-03-30 05:20] -> (A, 0000001) [2020-03-30 05:30]
  MUMO (payload=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 2: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]
leg 4: (C, 0000003) [2020-03-30 07:45] -> (END, END) [2020-03-30 08:00]
  MUMO (payload=77, duration=15)

[2020-03-30 05:50, 2020-03-30 08:30]
TRANSFERS: 1
     FROM: (START, START) [2020-03-30 05:50]
       TO: (END, END) [2020-03-30 08:30]
leg 0: (START, START) [2020-03-30 05:50] -> (A, 0000001) [2020-03-30 06:00]
  MUMO (payload=99, duration=10)
leg 1: (A, 0000001) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 07:00]
   0: 0000001 A...............................................                               d: 30.03 06:00 [30.03 08:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/360/0000002/420/, src=0}]
   1: 0000002 B............................................... a: 30.03 07:00 [30.03 09:00]
leg 2: (B, 0000002) [2020-03-30 07:00] -> (B, 0000002) [2020-03-30 07:02]
  FOOTPATH (duration=2)
leg 3: (B, 0000002) [2020-03-30 07:15] -> (C, 0000003) [2020-03-30 08:15]
   0: 0000002 B...............................................                               d: 30.03 07:15 [30.03 09:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/435/0000003/495/, src=0}]
   1: 0000003 C............................................... a: 30.03 08:15 [30.03 10:15]
leg 4: (C, 0000003) [2020-03-30 08:15] -> (END, END) [2020-03-30 08:30]
  MUMO (payload=77, duration=15)

)";

TEST(tb_query, intermodal_abc) {
  auto const tt = load_hrd(files_abc);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_intermodal_search(
      tt, tbd,
      {{tt.locations_.location_id_to_idx_.at(
            {.id_ = "0000001", .src_ = source_idx_t{0U}}),
        10_minutes, 99U}},
      {{tt.locations_.location_id_to_idx_.at(
            {.id_ = "0000003", .src_ = source_idx_t{0U}}),
        15_minutes, 77U}},
      interval{unixtime_t{sys_days{March / 30 / 2020}} + 5_hours,
               unixtime_t{sys_days{March / 30 / 2020}} + 6_hours});
  EXPECT_EQ(std::string_view{intermodal_abc_journeys},
            results_str(results, tt));
}

// One query_state for two queries: the first one's arrival times and
// destination offset (5 min) must not carry over to the second one (15 min).
TEST(tb_query, reuse_state) {
  auto const tt = load_hrd(files_abc);
  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto search_state = routing::search_state{};
  auto algo_state = tb::query_state{tt, tbd};
  auto const search = [&](duration_t const dest_offset) {
    auto q = routing::query{
        .start_time_ =
            interval{unixtime_t{sys_days{March / 30 / 2020}} + 5_hours,
                     unixtime_t{sys_days{March / 30 / 2020}} + 6_hours},
        .start_match_mode_ = routing::location_match_mode::kIntermodal,
        .dest_match_mode_ = routing::location_match_mode::kIntermodal,
        .start_ = {{tt.locations_.location_id_to_idx_.at(
                        {.id_ = "0000001", .src_ = source_idx_t{0U}}),
                    10_minutes, 99U}},
        .destination_ = {{tt.locations_.location_id_to_idx_.at(
                              {.id_ = "0000003", .src_ = source_idx_t{0U}}),
                          dest_offset, 77U}}};
    return *(routing::search<direction::kForward, tb::query_engine<false>>{
        tt, nullptr, search_state, algo_state, std::move(q)}
                 .execute()
                 .journeys_);
  };
  search(5_minutes);
  EXPECT_EQ(std::string_view{intermodal_abc_journeys},
            results_str(search(15_minutes), tt));
}
// R (RA -> RB -> SD) departs RA every minute from 00:00 to 71:59, so its
// route has 4 320 transports, three per minute of the day. S0 -> RA -> SD
// (1 transfer) boards R at 23:17, S0 -> M -> RB -> SD (2 transfers) at 05:32.
// With 12 bits for the transport offset in reached, reaching the 23:17
// transport (offset > 4 096) at RA pruned the 05:32 one at RB.
mem_dir many_transports_files() {
  auto s = std::string{R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
M,M,,,,,,
RA,RA,,,,,,
RB,RB,,,,,,
SD,SD,,,,,,

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
MON,1,0,0,0,0,0,0,20210301,20210307

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
P,DTA,P,P,,3
R,DTA,R,R,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
P,MON,P1,P1,
P,MON,P2,P2,
P,MON,P3,P3,
)"};
  auto constexpr kTrips = 3U * 1440U;
  for (auto i = 0U; i != kTrips; ++i) {
    s += fmt::format("R,MON,R{0},R{0},\n", i);
  }
  s += R"(
# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
P1,23:00:00,23:00:00,S0,0,0,0
P1,23:15:00,23:15:00,RA,1,0,0
P2,05:00:00,05:00:00,S0,0,0,0
P2,05:10:00,05:10:00,M,1,0,0
P3,05:15:00,05:15:00,M,0,0,0
P3,05:30:00,05:30:00,RB,1,0,0
)";
  auto const hhmm = [](unsigned const m) {
    return fmt::format("{:02}:{:02}:00", m / 60U, m % 60U);
  };
  for (auto i = 0U; i != kTrips; ++i) {
    s += fmt::format(
        "R{0},{1},{1},RA,0,0,0\nR{0},{2},{2},RB,1,0,0\nR{0},{3},{3},SD,2,0,0\n",
        i, hhmm(i), hhmm(i + 5U), hhmm(i + 10U));
  }
  return mem_dir::read(s);
}

TEST(tb_query, many_transports) {
  auto const tt = load_gtfs(many_transports_files);
  auto max_transports = 0U;
  for (auto r = route_idx_t{0U}; r != tt.n_routes(); ++r) {
    max_transports =
        std::max(max_transports, tt.route_transport_ranges_[r].size());
  }
  ASSERT_GT(max_transports, 4096U);

  auto const tbd = tb::preprocess(tt, profile_idx_t{0});
  auto const results = tripbased_search(
      tt, tbd, "S0", "SD", unixtime_t{sys_days{March / 1 / 2021} + 5h});
  ASSERT_NE(0U, results.size());
  auto earliest = results.begin()->dest_time_;
  for (auto const& j : results) {
    earliest = std::min(earliest, j.dest_time_);
  }
  EXPECT_EQ(unixtime_t{sys_days{March / 1 / 2021} + 5h + 37min}, earliest)
      << results_str(results, tt);
}
