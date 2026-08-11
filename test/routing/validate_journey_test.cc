#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/validate_journey.h"
#include "nigiri/rt/create_rt_timetable.h"
#include "nigiri/rt/gtfsrt_update.h"
#include "nigiri/rt/rt_timetable.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::routing;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;
using nigiri::test::raptor_search;

namespace {

// Two independent transfer pairs, same as raptor_rt_modes_test.cc's naive
// baseline fixture:
//   A -[T1]-> B -[T2 / T2B]-> D   2 min (same-stop) transfer buffer at B
//                                 => a delay on T1 breaks the connection.
//   E -[T3]-> F -[T4]---------> G 10 min transfer buffer at F
//                                 => a small delay on T3 does not break it.
mem_dir test_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Europe/Berlin

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.00,1.00,,
B,B,,0.02,1.03,,
C,C,,0.04,1.05,,
D,D,,0.06,1.07,,
E,E,,0.10,1.10,,
F,F,,0.12,1.13,,
G,G,,0.14,1.15,,

# calendar_dates.txt
service_id,date,exception_type
S1,20190503,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,R1,R1,,3
R2,DB,R2,R2,,3
R3,DB,R3,R3,,3
R4,DB,R4,R4,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S1,T1,T1,
R2,S1,T2,T2,
R2,S1,T2B,T2B,
R3,S1,T3,T3,
R4,S1,T4,T4,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1,09:00:00,09:00:00,A,1,0,0
T1,09:10:00,09:10:00,B,2,0,0
T2,09:12:00,09:12:00,B,1,0,0
T2,09:20:00,09:20:00,C,2,0,0
T2,09:30:00,09:30:00,D,3,0,0
T2B,10:30:00,10:30:00,B,1,0,0
T2B,10:40:00,10:40:00,C,2,0,0
T2B,10:50:00,10:50:00,D,3,0,0
T3,09:00:00,09:00:00,E,1,0,0
T3,09:10:00,09:10:00,F,2,0,0
T4,09:20:00,09:20:00,F,1,0,0
T4,09:30:00,09:30:00,G,2,0,0
)");
}

timetable load_tt() {
  timetable tt;
  tt.date_range_ = {date::sys_days{2019_y / March / 25},
                    date::sys_days{2019_y / November / 1}};
  load_timetable({}, source_idx_t{0}, test_files(), tt);
  finalize(tt);
  return tt;
}

transit_realtime::FeedMessage make_delay_msg(std::string const& trip_id,
                                             std::string const& start_date,
                                             std::string const& start_time,
                                             std::uint32_t const stop_sequence,
                                             std::int32_t const delay_minutes) {
  transit_realtime::FeedMessage msg;
  auto const hdr = msg.mutable_header();
  hdr->set_gtfs_realtime_version("2.0");
  hdr->set_incrementality(
      transit_realtime::FeedHeader_Incrementality_FULL_DATASET);
  hdr->set_timestamp(0U);

  auto const e = msg.add_entity();
  e->set_id("1");
  e->set_is_deleted(false);

  auto const td = e->mutable_trip_update()->mutable_trip();
  td->set_start_time(start_time);
  td->set_start_date(start_date);
  td->set_trip_id(trip_id);

  auto const stop_update = e->mutable_trip_update()->add_stop_time_update();
  stop_update->set_stop_sequence(stop_sequence);
  stop_update->mutable_arrival()->set_delay(delay_minutes * 60);

  return msg;
}

transit_realtime::FeedMessage make_cancel_msg(std::string const& trip_id,
                                              std::string const& start_date,
                                              std::string const& start_time) {
  transit_realtime::FeedMessage msg;
  auto const hdr = msg.mutable_header();
  hdr->set_gtfs_realtime_version("2.0");
  hdr->set_incrementality(
      transit_realtime::FeedHeader_Incrementality_FULL_DATASET);
  hdr->set_timestamp(0U);

  auto const e = msg.add_entity();
  e->set_id("1");
  e->set_is_deleted(false);

  auto const td = e->mutable_trip_update()->mutable_trip();
  td->set_start_time(start_time);
  td->set_start_date(start_date);
  td->set_trip_id(trip_id);
  td->set_schedule_relationship(
      transit_realtime::TripDescriptor_ScheduleRelationship_CANCELED);

  return msg;
}

interval<unixtime_t> whole_day() {
  return {sys_days{May / 3 / 2019}, sys_days{May / 3 / 2019} + 24h};
}

// The passenger's plan: always reconstructed against rtt == nullptr, so it
// never depends on whatever realtime state happens to already exist -- this
// is what a query::with_scheduled_comparison_ search's journeys_scheduled_
// would hand back.
journey get_planned_journey(timetable const& tt,
                            std::string_view const from = "A",
                            std::string_view const to = "D") {
  auto const planned = raptor_search(tt, nullptr, from, to, whole_day());
  EXPECT_EQ(1U, planned.size());
  return *planned.begin();
}

}  // namespace

TEST(routing, validate_journey_feasible_without_rt_update) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto const planned = get_planned_journey(tt);

  auto const result = validate_journey(tt, rtt, planned);

  EXPECT_TRUE(result.feasible_);
  EXPECT_FALSE(result.broken_leg_idx_.has_value());
  EXPECT_EQ(planned.dest_time_, result.arrival_time_);
}

TEST(routing, validate_journey_broken_by_delay) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto const planned = get_planned_journey(tt);
  ASSERT_EQ(3U, planned.legs_.size());  // T1, footpath B->B, T2

  // T1 arrives 40 min late at B (09:10 -> 09:50): misses T2 (dep 09:12).
  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const result = validate_journey(tt, rtt, planned);

  EXPECT_FALSE(result.feasible_);
  ASSERT_TRUE(result.broken_leg_idx_.has_value());
  EXPECT_EQ(2U, *result.broken_leg_idx_);  // the T2 leg couldn't be boarded
  EXPECT_NE(std::string::npos, result.reason_.find("missed connection"))
      << result.reason_;
  // How far the passenger actually gets: stuck at B, ready 40 min later
  // than planned (the footpath leg's original arrival, shifted by the
  // delay) -- not D, so comparing against planned.dest_time_ directly
  // wouldn't be meaningful (different location).
  EXPECT_EQ(planned.legs_[1].arr_time_ + 40min, result.arrival_time_);
}

TEST(routing, validate_journey_broken_by_cancellation) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto const planned = get_planned_journey(tt);
  ASSERT_EQ(3U, planned.legs_.size());

  auto const msg = make_cancel_msg("T2", "20190503", "09:12:00");
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const result = validate_journey(tt, rtt, planned);

  EXPECT_FALSE(result.feasible_);
  ASSERT_TRUE(result.broken_leg_idx_.has_value());
  EXPECT_EQ(2U, *result.broken_leg_idx_);
  EXPECT_NE(std::string::npos, result.reason_.find("cancelled"))
      << result.reason_;
}

// A delay that doesn't break the transfer (T3 -> T4 at F, 10 min buffer)
// must leave the plan feasible, with the same arrival as originally
// planned -- T4 itself isn't delayed, so the final arrival at G is
// unaffected even though T3 ran late.
TEST(routing, validate_journey_feasible_with_harmless_delay) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto const planned = get_planned_journey(tt, "E", "G");

  auto const msg = make_delay_msg("T3", "20190503", "09:00:00", 2U, 5);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const result = validate_journey(tt, rtt, planned);

  EXPECT_TRUE(result.feasible_);
  EXPECT_FALSE(result.broken_leg_idx_.has_value());
  EXPECT_EQ(planned.dest_time_, result.arrival_time_);
}
