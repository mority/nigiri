#include "gtest/gtest.h"

#include "nigiri/footpath.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/rt/create_rt_timetable.h"
#include "nigiri/rt/gtfsrt_update.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/td_footpath.h"

#include "../raptor_search.h"
#include "results_to_string.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::routing;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;
using nigiri::test::raptor_search;

namespace {

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

constexpr auto const kWholeDay =
    interval<unixtime_t>{sys_days{May / 3 / 2019},
                         sys_days{May / 3 / 2019} + 24h};

constexpr auto const kSrc = source_idx_t{0};

timetable load_tt(auto const& test_files) {
  timetable tt;
  register_special_stations(tt);
  tt.date_range_ = {date::sys_days{2019_y / March / 25},
                    date::sys_days{2019_y / November / 1}};
  load_timetable({}, kSrc, test_files, tt);
  finalize(tt);
  return tt;
}

//   A -[T1]-> B -[T2 / T2B]-> C -> D   5 min transfer buffer at B
mem_dir same_stop_test_files() {
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

# calendar_dates.txt
service_id,date,exception_type
S1,20190503,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,R1,R1,,3
R2,DB,R2,R2,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S1,T1,T1,
R2,S1,T2,T2,
R2,S1,T2B,T2B,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1,09:00:00,09:00:00,A,1,0,0
T1,09:10:00,09:10:00,B,2,0,0
T2,09:15:00,09:15:00,B,1,0,0
T2,09:20:00,09:20:00,C,2,0,0
T2,09:30:00,09:30:00,D,3,0,0
T2B,10:30:00,10:30:00,B,1,0,0
T2B,10:40:00,10:40:00,C,2,0,0
T2B,10:50:00,10:50:00,D,3,0,0
)");
}

// A -[T1]-> B --footpath(5 min)--> B2 -[T2 / T2B]-> D
mem_dir footpath_test_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Europe/Berlin

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.00,1.00,,
B,B,,0.02,1.03,,
B2,B2,,0.50,1.50,,
C,C,,0.04,1.05,,
D,D,,0.06,1.07,,

# transfers.txt
from_stop_id,to_stop_id,transfer_type,min_transfer_time
B,B2,2,300

# calendar_dates.txt
service_id,date,exception_type
S1,20190503,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,R1,R1,,3
R2,DB,R2,R2,,3

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S1,T1,T1,
R2,S1,T2,T2,
R2,S1,T2B,T2B,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1,09:00:00,09:00:00,A,1,0,0
T1,09:10:00,09:10:00,B,2,0,0
T2,09:16:00,09:16:00,B2,1,0,0
T2,09:24:00,09:24:00,C,2,0,0
T2,09:34:00,09:34:00,D,3,0,0
T2B,10:30:00,10:30:00,B2,1,0,0
T2B,10:40:00,10:40:00,C,2,0,0
T2B,10:50:00,10:50:00,D,3,0,0
)");
}

// independent routes between A and C, sharing no stops:
// A -[TAB1]-> B1 --footpath--> B2 -[TB2C]-> C   (scheduled optimal, 09:24)
// A -[TAD]--> D --------------------[TDC]-> C   (slower, 09:35 -- only
//                                                 becomes optimal once the
//                                                 B1->B2 transfer breaks)
mem_dir reroute_test_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Europe/Berlin

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.00,1.00,,
B1,B1,,0.02,1.03,,
B2,B2,,0.02,1.03,,
C,C,,0.04,1.05,,
D,D,,0.20,1.20,,

# transfers.txt
from_stop_id,to_stop_id,transfer_type,min_transfer_time
B1,B2,2,120

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
R1,S1,TAB1,TAB1,
R2,S1,TB2C,TB2C,
R3,S1,TAD,TAD,
R4,S1,TDC,TDC,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
TAB1,09:00:00,09:00:00,A,1,0,0
TAB1,09:10:00,09:10:00,B1,2,0,0
TB2C,09:14:00,09:14:00,B2,1,0,0
TB2C,09:24:00,09:24:00,C,2,0,0
TAD,09:00:00,09:00:00,A,1,0,0
TAD,09:15:00,09:15:00,D,2,0,0
TDC,09:17:00,09:17:00,D,1,0,0
TDC,09:35:00,09:35:00,C,2,0,0
)");
}

timetable load_reroute_tt_with_foot_profile() {
  auto tt = load_tt(reroute_test_files());

  tt.fwd_search_lb_graph_[kFootProfile] = tt.fwd_search_lb_graph_[kDefaultProfile];
  tt.bwd_search_lb_graph_[kFootProfile] = tt.bwd_search_lb_graph_[kDefaultProfile];

  auto const b1 = tt.locations_.location_id_to_idx_.at({"B1", kSrc});
  auto const b2 = tt.locations_.location_id_to_idx_.at({"B2", kSrc});

  tt.locations_.footpaths_out_[kFootProfile].resize(tt.n_locations());
  tt.locations_.footpaths_in_[kFootProfile].resize(tt.n_locations());
  tt.locations_.footpaths_out_[kFootProfile][b1].push_back(footpath{b2, 2min});
  tt.locations_.footpaths_in_[kFootProfile][b2].push_back(footpath{b1, 2min});

  return tt;
}

void enable_td_footpath_override(timetable const& tt,
                                 rt_timetable& rtt,
                                 duration_t const duration) {
  auto const b1 = tt.locations_.location_id_to_idx_.at({"B1", kSrc});
  auto const b2 = tt.locations_.location_id_to_idx_.at({"B2", kSrc});

  rtt.has_td_footpaths_out_[kFootProfile].set(b1, true);
  rtt.has_td_footpaths_in_[kFootProfile].set(b1, true);
  rtt.has_td_footpaths_out_[kFootProfile].set(b2, true);
  rtt.has_td_footpaths_in_[kFootProfile].set(b2, true);
  rtt.td_footpaths_out_[kFootProfile].resize(tt.n_locations());
  rtt.td_footpaths_in_[kFootProfile].resize(tt.n_locations());

  auto const valid_from = unixtime_t{sys_days{2019_y / May / 3}};
  rtt.td_footpaths_out_[kFootProfile][b1].push_back(
      td_footpath{b2, valid_from, duration});
  rtt.td_footpaths_in_[kFootProfile][b2].push_back(
      td_footpath{b1, valid_from, duration});
}

}  // namespace

TEST(rt_modes, align_without_rt_update_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const scheduled = raptor_search(tt, nullptr, "A", "D", kWholeDay);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);
  auto const dual = raptor_search(tt, &rtt, "A", "D", kWholeDay);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

TEST(rt_modes, separate_align_without_rt_update_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const scheduled =
      raptor_search(tt, nullptr, "D", "A", kWholeDay, direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

TEST(rt_modes, separate_delay_breaks_transfer_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const scheduled = raptor_search(tt, nullptr, "A", "D", kWholeDay);
  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  EXPECT_EQ(1U, scheduled.begin()->transfers_);
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h}),
            scheduled.begin()->departure_time());
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h + 30min}),
            scheduled.begin()->arrival_time());

  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  EXPECT_EQ(1U, realtime.begin()->transfers_);
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h}),
            realtime.begin()->departure_time());
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 8h + 50min}),
            realtime.begin()->arrival_time());
}

TEST(rt_modes, separate_delay_breaks_transfer_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const scheduled =
      raptor_search(tt, nullptr, "D", "A", kWholeDay, direction::kBackward);
  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  EXPECT_EQ(1U, scheduled.begin()->transfers_);
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h}),
            scheduled.begin()->departure_time());
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h + 30min}),
            scheduled.begin()->arrival_time());

  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  EXPECT_EQ(1U, realtime.begin()->transfers_);
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 7h}),
            realtime.begin()->departure_time());
  EXPECT_EQ((unixtime_t{sys_days{May / 3 / 2019} + 8h + 50min}),
            realtime.begin()->arrival_time());
}

TEST(rt_modes, separate_does_not_break_transfer_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 3);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const scheduled = raptor_search(tt, nullptr, "A", "D", kWholeDay);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

TEST(rt_modes, separate_does_not_break_transfer_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 3);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const scheduled =
      raptor_search(tt, nullptr, "D", "A", kWholeDay, direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);

  ASSERT_EQ(1U, scheduled.size()) << to_string(tt, scheduled);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);

  EXPECT_EQ(scheduled.begin()->transfers_, realtime.begin()->transfers_);
  EXPECT_EQ(scheduled.begin()->departure_time(),
            realtime.begin()->departure_time());
  EXPECT_EQ(scheduled.begin()->arrival_time(), realtime.begin()->arrival_time());
}

TEST(rt_modes, combined_align_without_rt_update_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto s_state = search_state{};
  auto a_state = raptor_state{};

  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kForward);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);

  ASSERT_NE(nullptr, combined.journeys_);
  ASSERT_NE(nullptr, combined.journeys_scheduled_);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, &rtt, *combined.journeys_),
            to_string(tt, &rtt, *combined.journeys_scheduled_));
}

TEST(rt_modes, combined_align_without_rt_update_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto s_state = search_state{};
  auto a_state = raptor_state{};

  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);

  ASSERT_NE(nullptr, combined.journeys_);
  ASSERT_NE(nullptr, combined.journeys_scheduled_);
  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, &rtt, *combined.journeys_),
            to_string(tt, &rtt, *combined.journeys_scheduled_));
}

TEST(rt_modes, combined_diverges_when_delay_breaks_transfer_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kForward);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);
  auto const sched = raptor_search(tt, nullptr, "A", "D", kWholeDay);

  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_EQ(1U, sched.size()) << to_string(tt, sched);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, sched),
            to_string(tt, *combined.journeys_scheduled_));
  EXPECT_EQ(combined.journeys_scheduled_->begin()->arrival_time() + 80min,
            combined.journeys_->begin()->arrival_time());
}

TEST(rt_modes, combined_diverges_when_delay_breaks_transfer_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);
  auto const sched =
      raptor_search(tt, nullptr, "D", "A", kWholeDay, direction::kBackward);

  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_EQ(1U, sched.size()) << to_string(tt, sched);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, sched),
            to_string(tt, *combined.journeys_scheduled_));
  EXPECT_EQ(combined.journeys_scheduled_->begin()->arrival_time() + 80min,
            combined.journeys_->begin()->arrival_time());
}

TEST(rt_modes, combined_diverges_via_footpath_fwd) {
  auto const tt = load_tt(footpath_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kForward);
  auto const realtime = raptor_search(tt, &rtt, "A", "D", kWholeDay);
  auto const sched = raptor_search(tt, nullptr, "A", "D", kWholeDay);

  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_EQ(1U, sched.size()) << to_string(tt, sched);
  ASSERT_EQ(1U, combined.journeys_->size());
  ASSERT_EQ(1U, combined.journeys_scheduled_->size());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, sched),
            to_string(tt, *combined.journeys_scheduled_));
  EXPECT_NE(combined.journeys_scheduled_->begin()->arrival_time(),
           combined.journeys_->begin()->arrival_time());
}

TEST(rt_modes, combined_diverges_via_footpath_bwd) {
  auto const tt = load_tt(footpath_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, a_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                             0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kBackward);
  auto const realtime =
      raptor_search(tt, &rtt, "D", "A", kWholeDay, direction::kBackward);
  auto const sched =
      raptor_search(tt, nullptr, "D", "A", kWholeDay, direction::kBackward);

  ASSERT_EQ(1U, realtime.size()) << to_string(tt, &rtt, realtime);
  ASSERT_EQ(1U, sched.size()) << to_string(tt, sched);
  ASSERT_EQ(1U, combined.journeys_->size());
  ASSERT_EQ(1U, combined.journeys_scheduled_->size());

  EXPECT_EQ(to_string(tt, &rtt, realtime), to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, sched),
            to_string(tt, *combined.journeys_scheduled_));
  EXPECT_NE(combined.journeys_scheduled_->begin()->arrival_time(),
           combined.journeys_->begin()->arrival_time());
}

TEST(rt_modes, combined_reroutes_delay_fwd) {
  auto const tt = load_tt(reroute_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("TAB1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const a = tt.locations_.location_id_to_idx_.at({"A", kSrc});
  auto const c = tt.locations_.location_id_to_idx_.at({"C", kSrc});

  auto s_state = search_state{};
  auto r_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kForward);

  auto rt_s_state = search_state{};
  auto rt_r_state = raptor_state{};
  auto const realtime = raptor_search(
      tt, &rtt, rt_s_state, rt_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}}},
      direction::kForward);

  auto sched_s_state = search_state{};
  auto sched_r_state = raptor_state{};
  auto const sched = raptor_search(
      tt, nullptr, sched_s_state, sched_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}}},
      direction::kForward);

  ASSERT_EQ(1U, realtime.journeys_->size())
      << to_string(tt, &rtt, *realtime.journeys_);
  ASSERT_EQ(1U, sched.journeys_->size())
      << to_string(tt, *sched.journeys_);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *realtime.journeys_),
            to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, *sched.journeys_),
            to_string(tt, *combined.journeys_scheduled_));

  auto const sched_str = to_string(tt, *combined.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *combined.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(rt_modes, combined_reroutes_delay_bwd) {
  auto const tt = load_tt(reroute_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("TAB1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto const a = tt.locations_.location_id_to_idx_.at({"A", kSrc});
  auto const c = tt.locations_.location_id_to_idx_.at({"C", kSrc});

  auto s_state = search_state{};
  auto r_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}},
           .with_scheduled_comparison_ = true},
      direction::kBackward);

  auto rt_s_state = search_state{};
  auto rt_r_state = raptor_state{};
  auto const realtime = raptor_search(
      tt, &rtt, rt_s_state, rt_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}}},
      direction::kBackward);

  auto sched_s_state = search_state{};
  auto sched_r_state = raptor_state{};
  auto const sched = raptor_search(
      tt, nullptr, sched_s_state, sched_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}}},
      direction::kBackward);

  ASSERT_EQ(1U, realtime.journeys_->size())
      << to_string(tt, &rtt, *realtime.journeys_);
  ASSERT_EQ(1U, sched.journeys_->size())
      << to_string(tt, *sched.journeys_);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *realtime.journeys_),
            to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, *sched.journeys_),
            to_string(tt, *combined.journeys_scheduled_));

  auto const sched_str = to_string(tt, *combined.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *combined.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(rt_modes, combined_reroutes_td_footpath_fwd) {
  auto const tt = load_reroute_tt_with_foot_profile();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  enable_td_footpath_override(tt, rtt, 20min);

  auto const a = tt.locations_.location_id_to_idx_.at({"A", kSrc});
  auto const c = tt.locations_.location_id_to_idx_.at({"C", kSrc});

  auto s_state = search_state{};
  auto r_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile,
           .with_scheduled_comparison_ = true},
      direction::kForward);

  auto rt_s_state = search_state{};
  auto rt_r_state = raptor_state{};
  auto const realtime = raptor_search(
      tt, &rtt, rt_s_state, rt_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile},
      direction::kForward);

  auto sched_s_state = search_state{};
  auto sched_r_state = raptor_state{};
  auto const sched = raptor_search(
      tt, nullptr, sched_s_state, sched_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{a, 0_minutes, 0U}},
           .destination_ = {{c, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile},
      direction::kForward);

  ASSERT_EQ(1U, realtime.journeys_->size())
      << to_string(tt, &rtt, *realtime.journeys_);
  ASSERT_EQ(1U, sched.journeys_->size())
      << to_string(tt, *sched.journeys_);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *realtime.journeys_),
            to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, *sched.journeys_),
            to_string(tt, *combined.journeys_scheduled_));

  auto const sched_str = to_string(tt, *combined.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *combined.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(routing, combined_reroutes_td_footpath_bwd) {
  auto const tt = load_reroute_tt_with_foot_profile();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  enable_td_footpath_override(tt, rtt, 20min);

  auto const a = tt.locations_.location_id_to_idx_.at({"A", kSrc});
  auto const c = tt.locations_.location_id_to_idx_.at({"C", kSrc});

  auto s_state = search_state{};
  auto r_state = raptor_state{};
  auto const combined = raptor_search(
      tt, &rtt, s_state, r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile,
           .with_scheduled_comparison_ = true},
      direction::kBackward);

  auto rt_s_state = search_state{};
  auto rt_r_state = raptor_state{};
  auto const realtime = raptor_search(
      tt, &rtt, rt_s_state, rt_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile},
      direction::kBackward);

  auto sched_s_state = search_state{};
  auto sched_r_state = raptor_state{};
  auto const sched = raptor_search(
      tt, nullptr, sched_s_state, sched_r_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{c, 0_minutes, 0U}},
           .destination_ = {{a, 0_minutes, 0U}},
           .prf_idx_ = kFootProfile},
      direction::kBackward);

  ASSERT_EQ(1U, realtime.journeys_->size())
      << to_string(tt, &rtt, *realtime.journeys_);
  ASSERT_EQ(1U, sched.journeys_->size())
      << to_string(tt, *sched.journeys_);
  ASSERT_FALSE(combined.journeys_->empty());
  ASSERT_FALSE(combined.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *realtime.journeys_),
            to_string(tt, &rtt, *combined.journeys_));
  EXPECT_EQ(to_string(tt, *sched.journeys_),
            to_string(tt, *combined.journeys_scheduled_));

  auto const sched_str = to_string(tt, *combined.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *combined.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(routing, pong_combined_align_without_rt_update_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};

  auto const result = pong_search(
      tt, &rtt, search_state, raptor_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                             0_minutes, 0U}},
           .min_connection_count_ = 1U,
           .with_scheduled_comparison_ = true},
      direction::kForward);

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, pong_combined_align_without_rt_update_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto const rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};

  auto const result = pong_search(
      tt, &rtt, search_state, raptor_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                             0_minutes, 0U}},
           .min_connection_count_ = 1U,
           .with_scheduled_comparison_ = true},
      direction::kBackward);

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, pong_combined_delay_breaks_transfer_fwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};
  auto const result = pong_search(
      tt, &rtt, search_state, raptor_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                             0_minutes, 0U}},
           .min_connection_count_ = 1U,
           .with_scheduled_comparison_ = true},
      direction::kForward);

  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(1U, result.journeys_scheduled_->begin()->transfers_);
  EXPECT_EQ(1U, result.journeys_->begin()->transfers_);

  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}

TEST(routing, pong_combined_delay_breaks_transfer_bwd) {
  auto const tt = load_tt(same_stop_test_files());
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, kSrc, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};
  auto const result = pong_search(
      tt, &rtt, search_state, raptor_state,
      query{.start_time_ = kWholeDay,
           .start_ = {{tt.locations_.location_id_to_idx_.at({"D", kSrc}),
                      0_minutes, 0U}},
           .destination_ = {{tt.locations_.location_id_to_idx_.at({"A", kSrc}),
                             0_minutes, 0U}},
           .min_connection_count_ = 1U,
           .with_scheduled_comparison_ = true},
      direction::kBackward);

  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(1U, result.journeys_scheduled_->begin()->transfers_);
  EXPECT_EQ(1U, result.journeys_->begin()->transfers_);

  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}
