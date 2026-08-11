#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/rt/create_rt_timetable.h"
#include "nigiri/rt/gtfsrt_update.h"
#include "nigiri/rt/rt_timetable.h"

#include "../raptor_search.h"
#include "results_to_string.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;
using nigiri::test::raptor_search;

namespace {

// Two independent transfer pairs, used as the baseline fixture for the
// planned "scheduled + realtime in one RAPTOR run" feature.
//
// Until that variant exists, "scheduled" and "realtime" results are produced
// by two independent raptor_search() calls (rtt == nullptr vs. rtt with RT
// updates applied) -- the naive approach described in the design. These
// tests pin down the expected behavior of that baseline; once the fused
// dual-slot RAPTOR lands, they should be extended to assert that its
// scheduled/rt outputs are bit-identical to these two independent searches.
//
//   A -[T1]-> B -[T2 / T2B]-> C -> D   2 min transfer buffer at B
//                                      => a delay on T1 breaks the T2
//                                         connection; T2B (much later) is
//                                         the realtime alternative.
//
//   E -[T3]-> F -[T4]-------------> G  10 min transfer buffer at F
//                                      => a small delay on T3 does not
//                                         break the T4 connection; scheduled
//                                         and realtime results coincide.
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

// Builds a GTFS-RT FeedMessage that delays a single trip's arrival at the
// given (1-based) stop_sequence by delay_minutes.
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

// A scalar unixtime_t start_time_ makes raptor_search perform a single
// ontrip earliest/latest-arrival query, which is not representative of how
// the routing API is normally used: with two same-transfer-count candidates
// tied on the ontrip criterion (here: T1's fixed departure from A), forward
// and backward search can each settle on a different one of them. A full-day
// interval query has no such tie -- both directions search the same range
// of possible journeys and must agree on what "best" means.
interval<unixtime_t> whole_day() {
  return {sys_days{May / 3 / 2019}, sys_days{May / 3 / 2019} + 24h};
}

}  // namespace

// All tests below only cover station-to-station queries; the _fwd/_bwd
// suffix names the search direction under test (nigiri::direction), not
// anything about the journey's real-world direction of travel.

// Baseline (no RT data at all): a scheduled-only search (rtt == nullptr)
// and a realtime search against an *empty* RT timetable must agree exactly.
// This is the "no delays" case: the passenger's planned journey is still
// the best journey on the day of travel.
TEST(routing, scheduled_vs_realtime_align_without_rt_update_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const start = whole_day();
  auto const scheduled = raptor_search(tt, nullptr, "A", "D", start);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", start);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  // Compare real-world departure/arrival (journey::departure_time() /
  // arrival_time()), not the raw start_time_/dest_time_ fields: those are
  // direction-relative (for backward search, start_time_ ends up holding
  // the real arrival and dest_time_ the real departure).
  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

// Same as _fwd, but as a backward (arrive-by) search: query "from" is the
// physical destination D, "to" is the physical origin A -- the convention
// nigiri::test::raptor_search uses for direction::kBackward.
TEST(routing, scheduled_vs_realtime_align_without_rt_update_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const start = whole_day();
  auto const scheduled =
      raptor_search(tt, nullptr, "D", "A", start, direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", start, direction::kBackward);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

// A delay large enough to break the planned transfer (T1 -> T2 at B, 2 min
// buffer) must leave the scheduled search untouched, while the realtime
// search reports the actual best alternative (T1 -> T2B).  This is the
// "your journey broke, here is what to do instead" case.
TEST(routing, scheduled_vs_realtime_diverge_when_delay_breaks_transfer_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // T1 arrives 40 min late at B (09:10 -> 09:50), which is well past T2's
  // 09:12 departure but still ahead of T2B's 10:30 departure.
  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const start = whole_day();
  auto const scheduled = raptor_search(tt, nullptr, "A", "D", start);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", start);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  // Scheduled search is unaffected by the RT update: still the T1 -> T2
  // connection, arriving on time.
  EXPECT_EQ(1U, scheduled.begin()->transfers_);

  // Realtime search reflects reality: T2 is missed, T2B is the best
  // remaining option, arriving 80 min later than the scheduled arrival
  // (T2B's 10:50 vs. T2's 09:30).
  EXPECT_EQ(1U, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->arrival_time() + 80min,
            realtime.begin()->arrival_time());
}

TEST(routing, scheduled_vs_realtime_diverge_when_delay_breaks_transfer_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const start = whole_day();
  auto const scheduled =
      raptor_search(tt, nullptr, "D", "A", start, direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", start, direction::kBackward);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(1U, scheduled.begin()->transfers_);
  EXPECT_EQ(1U, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->arrival_time() + 80min,
            realtime.begin()->arrival_time());
}

// A delay that does *not* break the planned transfer (T3 -> T4 at F, 10 min
// buffer) must leave both the scheduled and the realtime destination time
// identical: the journey the passenger planned is still exactly what
// happens, even though T3 itself ran late.
TEST(routing, scheduled_vs_realtime_align_when_delay_does_not_break_transfer_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // T3 arrives 5 min late at F (09:10 -> 09:15), still well ahead of T4's
  // 09:20 departure (2 min transfer buffer required).
  auto const msg = make_delay_msg("T3", "20190503", "09:00:00", 2U, 5);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const start = whole_day();
  auto const scheduled = raptor_search(tt, nullptr, "E", "G", start);
  auto const realtime = raptor_search(tt, &rtt, "E", "G", start);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

TEST(routing, scheduled_vs_realtime_align_when_delay_does_not_break_transfer_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T3", "20190503", "09:00:00", 2U, 5);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const start = whole_day();
  auto const scheduled =
      raptor_search(tt, nullptr, "G", "E", start, direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "G", "E", start, direction::kBackward);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}
