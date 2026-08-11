#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/raptor/pong.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/rt/create_rt_timetable.h"
#include "nigiri/rt/gtfsrt_update.h"
#include "nigiri/rt/rt_timetable.h"

#include "results_to_string.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::routing;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;

// query::with_scheduled_comparison_ (Stage 3-4) tested so far only went
// through search<>'s single interval sweep (search.h). pong_search is a
// structurally different entry point -- an alternating forward/backward
// ping-pong search where each ping result reseeds its own tiny pong
// sub-search -- reachable directly (not via raptor_search()/query's usual
// dispatch, only via routing::pong_search() itself, see e.g.
// test/rt/rt_delay_test.cc).
//
// pong.cc's pong_both implements this as: one PING using rt_mode::both
// (sharing the route scan the way search.h's dual-slot search does, finding
// scheduled- and realtime-optimal candidates together), then PONG
// separately per candidate -- a static-only (off) pong sub-search for every
// scheduled ping candidate, a realtime-aware (on) pong sub-search for every
// realtime ping candidate. It can't be a single fused pong sub-search:
// each is freshly re-seeded per ping journey via one raptor::add_start()
// call, which writes the same time into every slot, so a scheduled and a
// realtime ping candidate with different destination times can't share one
// seeding pass. See pong_both's own comment for the full reasoning
// (including why only the realtime pong gets ping-bounds pruning).
namespace {

// Same tight-buffer transfer pair as dual_slot_raptor_test.cc:
// A -[T1]-> B -[T2 / T2B]-> D, 2 min transfer buffer at B.
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
T2,09:12:00,09:12:00,B,1,0,0
T2,09:20:00,09:20:00,C,2,0,0
T2,09:30:00,09:30:00,D,3,0,0
T2B,10:30:00,10:30:00,B,1,0,0
T2B,10:40:00,10:40:00,C,2,0,0
T2B,10:50:00,10:50:00,D,3,0,0
)");
}

timetable load_tt() {
  timetable tt;
  register_special_stations(tt);
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

interval<unixtime_t> whole_day() {
  return {sys_days{May / 3 / 2019}, sys_days{May / 3 / 2019} + 24h};
}

// query.start_/destination_ is direction-relative: for backward search,
// "start_" is the physical destination D and "destination_" is the
// physical origin A (same convention as dual_slot_raptor_test.cc).
query make_query(timetable const& tt, direction const dir) {
  auto const src = source_idx_t{0};
  auto const a = tt.locations_.location_id_to_idx_.at({"A", src});
  auto const d = tt.locations_.location_id_to_idx_.at({"D", src});
  auto const [algo_start, algo_dest] =
      dir == direction::kForward ? std::pair{a, d} : std::pair{d, a};
  return query{.start_time_ = whole_day(),
              .start_ = {{algo_start, 0_minutes, 0U}},
              .destination_ = {{algo_dest, 0_minutes, 0U}},
              .min_connection_count_ = 1U,
              .with_scheduled_comparison_ = true};
}

}  // namespace

TEST(routing, pong_dual_slot_align_without_rt_update_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};

  auto const result =
      pong_search(tt, &rtt, search_state, raptor_state,
                 make_query(tt, direction::kForward), direction::kForward);

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, pong_dual_slot_align_without_rt_update_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};

  auto const result =
      pong_search(tt, &rtt, search_state, raptor_state,
                 make_query(tt, direction::kBackward), direction::kBackward);

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, pong_dual_slot_diverges_when_delay_breaks_transfer_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // T1 arrives 40 min late at B (09:10 -> 09:50): misses T2 (dep 09:12),
  // still makes T2B (dep 10:30).
  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};
  auto const result =
      pong_search(tt, &rtt, search_state, raptor_state,
                 make_query(tt, direction::kForward), direction::kForward);

  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(1U, result.journeys_scheduled_->begin()->transfers_);
  EXPECT_EQ(1U, result.journeys_->begin()->transfers_);

  // sched slot: unaffected, T1 -> T2, arriving on time.
  // rt slot: T2 missed, T2B is the best remaining option, 80 min later.
  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}

TEST(routing, pong_dual_slot_diverges_when_delay_breaks_transfer_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto search_state = routing::search_state{};
  auto raptor_state = routing::raptor_state{};
  auto const result =
      pong_search(tt, &rtt, search_state, raptor_state,
                 make_query(tt, direction::kBackward), direction::kBackward);

  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(1U, result.journeys_scheduled_->begin()->transfers_);
  EXPECT_EQ(1U, result.journeys_->begin()->transfers_);

  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}
