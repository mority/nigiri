// Equivalent stations are *not* part of a terminal.
//
// `get_metas` links same-named stops within 500 m, so equivalence is not
// transitive: with three stops 400 m apart in a line, the outer two are
// equivalent to the middle one but not to each other. The lb search seeds its
// terminals at the roots of the query's own expansion (here: DEST and DEST_MID)
// and used to let `get_alternative` re-expand such a terminal with
// `kEquivalent`, which reaches DEST_FAR - a stop the query never named - and
// hands it the terminal's offset, zero. A train arriving at DEST_FAR then
// counted as arriving at the destination, for free.

#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/files.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/lb_raptor/bidir_lb_raptor.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/search.h"

#include "../raptor_search.h"
#include "../util.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;

namespace {

// DEST (50.0000) - 400 m - DEST_MID (50.0036) - 400 m - DEST_FAR (50.0072),
// all called "Dest Hbf". Three trains from ORIG: RC reaches DEST_FAR at 09:00,
// RB the equivalent DEST_MID at 09:10, RA the queried DEST at 09:30. A journey
// ending at DEST_MID is legitimate (the query's expansion contains it), a
// journey that treats arriving at DEST_FAR as arriving there is not: the walk
// DEST_FAR -> DEST_MID takes 5 minutes, so 09:05 is the true optimum.
mem_dir equivalence_tt() {
  return mem_dir::read(R"__(
"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
MTA,MOTIS Transit Authority,https://motis-project.de/,Europe/Berlin

# calendar_dates.txt
service_id,date,exception_type
SID,20260227,1

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
ORIG,Orig,,50.1000,8.0000,,1,
ORIG_P,Orig,,50.1000,8.0000,,0,ORIG
DEST,Dest Hbf,,50.0000,8.0000,,,
DEST_MID,Dest Hbf,,50.0036,8.0000,,,
DEST_FAR,Dest Hbf,,50.0072,8.0000,,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
RC,MTA,RC,RC,ORIG -> DEST_FAR,2
RB,MTA,RB,RB,ORIG -> DEST_MID,2
RA,MTA,RA,RA,ORIG -> DEST,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
RC,SID,RC,RC,0
RB,SID,RB,RB,1
RA,SID,RA,RA,2

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
RC,08:00,08:00,ORIG_P,0,0,0
RC,09:00,09:00,DEST_FAR,1,0,0
RB,08:05,08:05,ORIG_P,0,0,0
RB,09:10,09:10,DEST_MID,1,0,0
RA,08:10,08:10,ORIG_P,0,0,0
RA,09:30,09:30,DEST,1,0,0
)__");
}

constexpr auto const kDay = 2026_y / February / 27;

timetable load() {
  return load_gtfs(equivalence_tt,
                   interval{sys_days{kDay}, sys_days{kDay} + date::days{2}});
}

location_idx_t find_loc(timetable const& tt, std::string_view id) {
  return tt.locations_.location_id_to_idx_.at(
      {.id_ = id, .src_ = source_idx_t{0}});
}

}  // namespace

TEST(routing, lb_raptor_terminal_does_not_swallow_equivalences) {
  auto const tt = load();

  auto const dest = find_loc(tt, "DEST");
  auto const mid = find_loc(tt, "DEST_MID");
  auto const far = find_loc(tt, "DEST_FAR");

  // the fixture only makes sense with the expected (non-transitive) metas
  auto const has_eq = [&](location_idx_t const a, location_idx_t const b) {
    auto const& eq = tt.locations_.equivalences_[a];
    return std::find(begin(eq), end(eq), b) != end(eq);
  };
  ASSERT_TRUE(has_eq(dest, mid));
  ASSERT_TRUE(has_eq(mid, far));
  ASSERT_FALSE(has_eq(dest, far));

  auto q = query{.start_time_ = unixtime_t{sys_days{kDay}} + 6_hours,
                 .start_match_mode_ = location_match_mode::kEquivalent,
                 .dest_match_mode_ = location_match_mode::kEquivalent,
                 .use_start_footpaths_ = true,
                 .start_ = {{find_loc(tt, "ORIG"), 0_minutes, 0U}},
                 .destination_ = {{dest, 0_minutes, 0U}}};

  auto s = search_state{};
  auto const r = bidir_lb_raptor_search(tt, nullptr, s, q, direction::kForward);
  auto const js = std::vector<journey>{begin(*r.journeys_), end(*r.journeys_)};
  ASSERT_FALSE(js.empty());

  // The query names the station ORIG, the trains depart from its platform
  // ORIG_P. Moving between the two is free and instantaneous, so it must not
  // show up as a leg - RAPTOR reports none either.
  for (auto const& j : js) {
    EXPECT_TRUE(
        std::holds_alternative<journey::run_enter_exit>(j.legs_.front().uses_))
        << "journey starts with a zero-length walk instead of the transport";
  }

  // Whatever it finds, it must not arrive at DEST_FAR and call that the
  // destination: the walk from there has to be paid for.
  auto const ref =
      nigiri::test::raptor_search(tt, nullptr, q, direction::kForward);
  ASSERT_FALSE(ref.empty());
  auto const best = ref.begin()->arrival_time();
  for (auto const& j : js) {
    EXPECT_GE(j.arrival_time(), best)
        << "lb beats RAPTOR: arrives " << j.arrival_time() << " instead of "
        << best;
    for (auto const& l : j.legs_) {
      if (!std::holds_alternative<journey::run_enter_exit>(l.uses_)) {
        EXPECT_TRUE(l.from_ == l.to_ || l.dep_time_ != l.arr_time_)
            << "free hop from " << l.from_ << " to " << l.to_;
      }
    }
    EXPECT_NE(far, j.legs_.back().to_) << "journey ends at DEST_FAR";
  }
}

// `init` seeds the terminals from the query's own expansion. Expanding those
// roots a second time (as it used to) reaches `equivalences_[root]` - here
// DEST_FAR, which the query never named. Nothing wrong comes out of it because
// `find_offset` rejects such a pattern at realization, so this is the only
// place the difference is visible: the patterns are reconstructed and thrown
// away again.
TEST(routing, lb_raptor_init_seeds_only_the_query_expansion) {
  auto const tt = load();

  auto const dest = find_loc(tt, "DEST");
  auto const mid = find_loc(tt, "DEST_MID");
  auto const far = find_loc(tt, "DEST_FAR");

  auto q = query{.start_time_ = unixtime_t{sys_days{kDay}} + 6_hours,
                 .start_match_mode_ = location_match_mode::kEquivalent,
                 .dest_match_mode_ = location_match_mode::kEquivalent,
                 .use_start_footpaths_ = true,
                 .start_ = {{find_loc(tt, "ORIG"), 0_minutes, 0U}},
                 .destination_ = {{dest, 0_minutes, 0U}}};
  q.sanitize(tt);

  auto lbr = bidir_lb_raptor{};
  lbr.execute(tt, nullptr, q);

  // The lb graph works on station complexes, and DEST - DEST_MID - DEST_FAR are
  // one: equivalence is not transitive, the complex is. So the terminal is that
  // complex, and what has to hold is that nothing *outside* it is a terminal -
  // reaching DEST_FAR itself is still not free, `get_alternative` only boards
  // and alights at the members the query's own expansion names (the test
  // above).
  EXPECT_EQ(tt.get_complex_idx(dest), tt.get_complex_idx(mid));
  EXPECT_EQ(tt.get_complex_idx(dest), tt.get_complex_idx(far));
  EXPECT_TRUE(lbr.is_dest_.test(to_idx(tt.get_complex_idx(dest))));
  EXPECT_FALSE(
      lbr.is_dest_.test(to_idx(tt.get_complex_idx(find_loc(tt, "ORIG")))))
      << "the origin is not a destination terminal";
}
