#include "gtest/gtest.h"

#include "nigiri/footpath.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
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

// End-to-end test of query::with_scheduled_comparison_ (Stage 3): a single
// search that produces both the realtime-best journeys (routing_result::
// journeys_) and, alongside them, what the best journeys would have been
// using only the static timetable (routing_result::journeys_scheduled_).
//
// _fwd/_bwd names the search direction under test, not the journey's
// real-world direction of travel (see scheduled_vs_realtime_test.cc, whose
// fixture and delay-message helper this test reuses).
namespace {

// Same tight-buffer transfer pair as scheduled_vs_realtime_test.cc:
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

// A scalar unixtime_t start_time_ makes the search perform a single ontrip
// query, which is not representative of how the routing API is normally
// used -- see scheduled_vs_realtime_test.cc. Always search a full-day
// interval instead.
interval<unixtime_t> whole_day() {
  return {sys_days{May / 3 / 2019}, sys_days{May / 3 / 2019} + 24h};
}

// Footpath-based transfer pair (distinct from the same-stop transfer above:
// this exercises update_footpaths, not update_transfers):
// A -[T1]-> B --footpath(5 min)--> B2 -[T2 / T2B]-> D, 1 min buffer after
// the footpath.
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

timetable load_footpath_tt() {
  timetable tt;
  tt.date_range_ = {date::sys_days{2019_y / March / 25},
                    date::sys_days{2019_y / November / 1}};
  load_timetable({}, source_idx_t{0}, footpath_test_files(), tt);
  finalize(tt);
  return tt;
}

// query.start_/destination_ is direction-relative, not physical: for
// backward search, "start_" is the physical destination D and
// "destination_" is the physical origin A -- the same convention
// nigiri::test::raptor_search uses for direction::kBackward.
query make_query(timetable const& tt,
                 direction const dir,
                 interval<unixtime_t> const& start) {
  auto const src = source_idx_t{0};
  auto const a = tt.locations_.location_id_to_idx_.at({"A", src});
  auto const d = tt.locations_.location_id_to_idx_.at({"D", src});
  auto const [algo_start, algo_dest] =
      dir == direction::kForward ? std::pair{a, d} : std::pair{d, a};
  return query{.start_time_ = start,
              .start_ = {{algo_start, 0_minutes, 0U}},
              .destination_ = {{algo_dest, 0_minutes, 0U}},
              .with_scheduled_comparison_ = true};
}

routing_result run_both(timetable const& tt,
                        rt_timetable const& rtt,
                        direction const dir,
                        search_state& s_state,
                        raptor_state& a_state) {
  return raptor_search(tt, &rtt, s_state, a_state,
                       make_query(tt, dir, whole_day()), dir);
}

// Two competing OD-independent routes between A and C, sharing no stops:
// A -[TAB1]-> B1 --footpath--> B2 -[TB2C]-> C   (scheduled optimal, 09:24)
// A -[TAD]--> D --------------------[TDC]-> C   (slower, 09:35 -- only
//                                                 becomes optimal once the
//                                                 B1->B2 transfer breaks)
// The B1->B2 footpath lives on a non-default profile (kFootProfile) so it
// can be overridden by a td_footpath in the td_footpath variant below;
// that requires a hand-built static footpath graph and lower-bound graph
// for that profile too (see load_reroute_tt), same as td_footpath_test.cc.
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

timetable load_reroute_tt() {
  timetable tt;
  tt.date_range_ = {date::sys_days{2019_y / March / 25},
                    date::sys_days{2019_y / November / 1}};
  load_timetable({}, source_idx_t{0}, reroute_test_files(), tt);
  finalize(tt);

  // kFootProfile has no static footpath/lower-bound graph of its own by
  // default (only kDefaultProfile is built by the GTFS loader) -- borrow
  // the default profile's lower-bound graph (an admissible pruning
  // heuristic, doesn't need to be exact) and hand-build the footpath graph,
  // same as test/routing/td_footpath_test.cc.
  tt.fwd_search_lb_graph_[kFootProfile] = tt.fwd_search_lb_graph_[kDefaultProfile];
  tt.bwd_search_lb_graph_[kFootProfile] = tt.bwd_search_lb_graph_[kDefaultProfile];

  auto const src = source_idx_t{0};
  auto const b1 = tt.locations_.location_id_to_idx_.at({"B1", src});
  auto const b2 = tt.locations_.location_id_to_idx_.at({"B2", src});

  tt.locations_.footpaths_out_[kFootProfile].resize(tt.n_locations());
  tt.locations_.footpaths_in_[kFootProfile].resize(tt.n_locations());
  tt.locations_.footpaths_out_[kFootProfile][b1].push_back(footpath{b2, 2min});
  tt.locations_.footpaths_in_[kFootProfile][b2].push_back(footpath{b1, 2min});

  return tt;
}

// query.start_/destination_ is direction-relative (see make_query above);
// always uses kFootProfile so the B1->B2 footpath (static or, once enabled,
// td) is actually consulted.
query make_reroute_query(timetable const& tt,
                         direction const dir,
                         interval<unixtime_t> const& start,
                         bool const with_scheduled_comparison) {
  auto const src = source_idx_t{0};
  auto const a = tt.locations_.location_id_to_idx_.at({"A", src});
  auto const c = tt.locations_.location_id_to_idx_.at({"C", src});
  auto const [algo_start, algo_dest] =
      dir == direction::kForward ? std::pair{a, c} : std::pair{c, a};
  return query{.start_time_ = start,
              .start_ = {{algo_start, 0_minutes, 0U}},
              .destination_ = {{algo_dest, 0_minutes, 0U}},
              .prf_idx_ = kFootProfile,
              .with_scheduled_comparison_ = with_scheduled_comparison};
}

routing_result run_both_reroute(timetable const& tt,
                                rt_timetable const& rtt,
                                direction const dir,
                                search_state& s_state,
                                raptor_state& a_state) {
  return raptor_search(tt, &rtt, s_state, a_state,
                       make_reroute_query(tt, dir, whole_day(), true), dir);
}

pareto_set<journey> run_reroute_baseline(timetable const& tt,
                                         rt_timetable const* rtt,
                                         direction const dir) {
  return raptor_search(tt, rtt,
                       make_reroute_query(tt, dir, whole_day(), false), dir);
}

// Enables the td_footpath override on B1<->B2 for kFootProfile: from
// midnight of the test day onward, the walk takes `duration` instead of the
// static graph's 2 min. Mirrors test/routing/td_footpath_test.cc's setup
// (has_td_footpaths_* must be set on both stops/both directions even though
// only B1->out and B2->in ever get read, by forward and backward search
// respectively).
void enable_td_footpath_override(timetable const& tt,
                                 rt_timetable& rtt,
                                 duration_t const duration) {
  auto const src = source_idx_t{0};
  auto const b1 = tt.locations_.location_id_to_idx_.at({"B1", src});
  auto const b2 = tt.locations_.location_id_to_idx_.at({"B2", src});

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

TEST(routing, dual_slot_raptor_align_without_rt_update_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto s_state = search_state{};
  auto a_state = raptor_state{};

  auto const result = run_both(tt, rtt, direction::kForward, s_state, a_state);
  auto const baseline = raptor_search(tt, &rtt, "A", "D", whole_day());

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_EQ(1U, baseline.size()) << to_string(tt, &rtt, baseline);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, dual_slot_raptor_align_without_rt_update_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});
  auto s_state = search_state{};
  auto a_state = raptor_state{};

  auto const result =
      run_both(tt, rtt, direction::kBackward, s_state, a_state);
  auto const baseline =
      raptor_search(tt, &rtt, "D", "A", whole_day(), direction::kBackward);

  ASSERT_NE(nullptr, result.journeys_);
  ASSERT_NE(nullptr, result.journeys_scheduled_);
  ASSERT_EQ(1U, baseline.size()) << to_string(tt, &rtt, baseline);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, &rtt, *result.journeys_),
            to_string(tt, &rtt, *result.journeys_scheduled_));
}

TEST(routing, dual_slot_raptor_diverges_when_delay_breaks_transfer_fwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // T1 arrives 40 min late at B (09:10 -> 09:50): misses T2 (dep 09:12),
  // still makes T2B (dep 10:30).
  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result = run_both(tt, rtt, direction::kForward, s_state, a_state);
  auto const baseline_on = raptor_search(tt, &rtt, "A", "D", whole_day());
  auto const baseline_off = raptor_search(tt, nullptr, "A", "D", whole_day());

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  // rt slot: exactly what a plain rt_mode::on search finds (T1 -> T2B).
  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));

  // sched slot: exactly what a plain rt_mode::off search finds (T1 -> T2,
  // unaffected by the delay) -- real reconstruction now, not a raw
  // round_times_ comparison.
  // Printed without rtt, like baseline_off itself: journey::print() looks
  // up RT annotations independently of how the journey was reconstructed,
  // so passing rtt here would show delay info baseline_off never sees.
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  // The two slots genuinely diverge: T2B arrives 80 min after T2.
  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}

TEST(routing, dual_slot_raptor_diverges_when_delay_breaks_transfer_bwd) {
  auto const tt = load_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 40);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both(tt, rtt, direction::kBackward, s_state, a_state);
  auto const baseline_on =
      raptor_search(tt, &rtt, "D", "A", whole_day(), direction::kBackward);
  auto const baseline_off =
      raptor_search(tt, nullptr, "D", "A", whole_day(), direction::kBackward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  // Printed without rtt, like baseline_off itself: journey::print() looks
  // up RT annotations independently of how the journey was reconstructed,
  // so passing rtt here would show delay info baseline_off never sees.
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  EXPECT_EQ(result.journeys_scheduled_->begin()->arrival_time() + 80min,
            result.journeys_->begin()->arrival_time());
}

// Closes the update_footpaths gap flagged after the initial dual-slot
// implementation: the transfer here is a genuine footpath between two
// distinct stops (B -> B2), not a same-stop transfer (which goes through
// update_transfers, exercised by the tests above).
TEST(routing, dual_slot_raptor_diverges_via_footpath_fwd) {
  auto const tt = load_footpath_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // T1 arrives 10 min late at B (09:10 -> 09:20): footpath-ready at B2 by
  // 09:25, past T2's 09:16 departure, but T2B (dep 10:30) still works.
  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result = run_both(tt, rtt, direction::kForward, s_state, a_state);
  auto const baseline_on = raptor_search(tt, &rtt, "A", "D", whole_day());
  auto const baseline_off = raptor_search(tt, nullptr, "A", "D", whole_day());

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));
  EXPECT_NE(result.journeys_scheduled_->begin()->arrival_time(),
           result.journeys_->begin()->arrival_time());
}

TEST(routing, dual_slot_raptor_diverges_via_footpath_bwd) {
  auto const tt = load_footpath_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("T1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both(tt, rtt, direction::kBackward, s_state, a_state);
  auto const baseline_on =
      raptor_search(tt, &rtt, "D", "A", whole_day(), direction::kBackward);
  auto const baseline_off =
      raptor_search(tt, nullptr, "D", "A", whole_day(), direction::kBackward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));
  EXPECT_NE(result.journeys_scheduled_->begin()->arrival_time(),
           result.journeys_->begin()->arrival_time());
}

// Both tests below share one scenario: the scheduled-optimal journey is
// A -> B1 -> B2 -> C (via TAB1 + footpath + TB2C), but realtime, the
// B1->B2 transfer breaks and the actual best journey reroutes entirely --
// different trips, different intermediate station (A -> D -> C via TAD +
// TDC) -- not just a later run of the same route, unlike the tests above.
// _delay breaks it via a vehicle delay (update_route / is_transport_active);
// _td_footpath breaks it via a realtime-only td_footpath override at B1
// (update_td_offsets) with no delay at all -- closing the loop on the
// design decision that the sched slot must never consult td_footpaths
// (see raptor.h's update_footpaths: the sched pass always uses the static
// footpath graph, ignoring rtt_ entirely).
TEST(routing, dual_slot_raptor_reroutes_due_to_delay_fwd) {
  auto const tt = load_reroute_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // TAB1 arrives 10 min late at B1 (09:10 -> 09:20): footpath-ready at B2
  // by 09:22, past TB2C's 09:14 departure -- no other B2->C trip exists.
  auto const msg = make_delay_msg("TAB1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both_reroute(tt, rtt, direction::kForward, s_state, a_state);
  auto const baseline_on = run_reroute_baseline(tt, &rtt, direction::kForward);
  auto const baseline_off =
      run_reroute_baseline(tt, nullptr, direction::kForward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  // Different transfer station, not just a different departure: scheduled
  // rides TB2C via B1/B2, realtime rides TDC via D instead.
  auto const sched_str = to_string(tt, *result.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *result.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(routing, dual_slot_raptor_reroutes_due_to_delay_bwd) {
  auto const tt = load_reroute_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  auto const msg = make_delay_msg("TAB1", "20190503", "09:00:00", 2U, 10);
  auto const stats = rt::gtfsrt_update_msg(tt, rtt, source_idx_t{0}, "tag", msg);
  ASSERT_EQ(stats.total_entities_success_, 1U);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both_reroute(tt, rtt, direction::kBackward, s_state, a_state);
  auto const baseline_on = run_reroute_baseline(tt, &rtt, direction::kBackward);
  auto const baseline_off =
      run_reroute_baseline(tt, nullptr, direction::kBackward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  auto const sched_str = to_string(tt, *result.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *result.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(routing, dual_slot_raptor_reroutes_due_to_td_footpath_fwd) {
  auto const tt = load_reroute_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  // No delay anywhere: TAB1 arrives B1 exactly on time (09:10). The B1->B2
  // walk itself becomes too slow in realtime (2 min -> 20 min), missing
  // TB2C's 09:14 departure just the same.
  enable_td_footpath_override(tt, rtt, 20min);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both_reroute(tt, rtt, direction::kForward, s_state, a_state);
  auto const baseline_on = run_reroute_baseline(tt, &rtt, direction::kForward);
  auto const baseline_off =
      run_reroute_baseline(tt, nullptr, direction::kForward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  // Scheduled slot never consults td_footpaths -- still the static 2 min
  // B1->B2 walk, unaffected by the realtime-only override.
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  auto const sched_str = to_string(tt, *result.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *result.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}

TEST(routing, dual_slot_raptor_reroutes_due_to_td_footpath_bwd) {
  auto const tt = load_reroute_tt();
  auto rtt = rt::create_rt_timetable(tt, date::sys_days{2019_y / May / 3});

  enable_td_footpath_override(tt, rtt, 20min);

  auto s_state = search_state{};
  auto a_state = raptor_state{};
  auto const result =
      run_both_reroute(tt, rtt, direction::kBackward, s_state, a_state);
  auto const baseline_on = run_reroute_baseline(tt, &rtt, direction::kBackward);
  auto const baseline_off =
      run_reroute_baseline(tt, nullptr, direction::kBackward);

  ASSERT_EQ(1U, baseline_on.size()) << to_string(tt, &rtt, baseline_on);
  ASSERT_EQ(1U, baseline_off.size()) << to_string(tt, baseline_off);
  ASSERT_FALSE(result.journeys_->empty());
  ASSERT_FALSE(result.journeys_scheduled_->empty());

  EXPECT_EQ(to_string(tt, &rtt, baseline_on), to_string(tt, &rtt, *result.journeys_));
  EXPECT_EQ(to_string(tt, baseline_off),
            to_string(tt, *result.journeys_scheduled_));

  auto const sched_str = to_string(tt, *result.journeys_scheduled_);
  auto const rt_str = to_string(tt, &rtt, *result.journeys_);
  EXPECT_NE(std::string::npos, sched_str.find("TB2C")) << sched_str;
  EXPECT_EQ(std::string::npos, sched_str.find("TDC")) << sched_str;
  EXPECT_NE(std::string::npos, rt_str.find("TDC")) << rt_str;
  EXPECT_EQ(std::string::npos, rt_str.find("TB2C")) << rt_str;
}
