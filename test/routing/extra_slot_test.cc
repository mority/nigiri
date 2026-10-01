#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <vector>

#include "fmt/format.h"

#include "nigiri/loader/dir.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/journey.h"
#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;
using nigiri::test::raptor_search;

// Earliest arrival query with one extra arrival time per label
// (query::extra_slot_). Round k keeps, per stop, the best arrival time and
// one later arrival time that is
//   - later than the best of round k, by at most 255 minutes,
//   - earlier than everything reached with fewer transfers.
// journey::slot_ = 1 marks journeys that end in the extra slot.
// A label takes the earliest trip of a route only: a later trip of the same
// route from the same label is no second journey.

namespace {

constexpr auto const kHeader = R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Etc/UTC

# calendar_dates.txt
service_id,date,exception_type
S,20190501,1
)";

timetable load(std::string const& files) {
  auto tt = timetable{};
  tt.date_range_ = {sys_days{2019_y / April / 30}, sys_days{2019_y / May / 3}};
  register_special_stations(tt);
  load_timetable({}, source_idx_t{0}, mem_dir::read(kHeader + files), tt);
  finalize(tt);
  return tt;
}

constexpr auto const kDay = sys_days{2019_y / May / 1};

routing::query make_query(timetable const& tt,
                          std::string_view const from,
                          std::string_view const to,
                          unixtime_t const start_time,
                          bool const extra_slot) {
  return routing::query{
      .start_time_ = start_time,
      .start_match_mode_ = routing::location_match_mode::kEquivalent,
      .dest_match_mode_ = routing::location_match_mode::kEquivalent,
      .use_start_footpaths_ = false,
      .start_ = {{tt.find(location_id{from, source_idx_t{0}}).value(), 0min,
                  0U}},
      .destination_ = {{tt.find(location_id{to, source_idx_t{0}}).value(),
                        0min, 0U}},
      .extra_slot_ = extra_slot};
}

// "11:00 1 slot=0 | F2 S>B1 | T B1>D"
std::string to_str(timetable const& tt, routing::journey const& j) {
  auto s = fmt::format("{} {} slot={}", date::format("%H:%M", j.arrival_time()),
                       j.transfers_, j.slot_);
  for (auto const& l : j.legs_) {
    if (std::holds_alternative<routing::journey::run_enter_exit>(l.uses_)) {
      auto const& r = std::get<routing::journey::run_enter_exit>(l.uses_).r_;
      s += fmt::format(" | {} {}>{} {}-{}", tt.transport_name(r.t_.t_idx_),
                       tt.locations_.ids_[l.from_].view(),
                       tt.locations_.ids_[l.to_].view(),
                       date::format("%H:%M", l.dep_time_),
                       date::format("%H:%M", l.arr_time_));
    }
  }
  return s;
}

using strings = std::vector<std::string>;

strings search(timetable const& tt,
               std::string_view const from,
               std::string_view const to,
               unixtime_t const start_time,
               bool const extra_slot,
               direction const dir = direction::kForward) {
  auto v = strings{};
  for (auto const& j :
       raptor_search(tt, nullptr,
                     make_query(tt, from, to, start_time, extra_slot), dir)) {
    EXPECT_TRUE(j.is_reconstructed_);
    EXPECT_FALSE(j.error_);
    // legs are connected and end at the arrival time of the journey
    for (auto i = 1U; i < j.legs_.size(); ++i) {
      EXPECT_LE(j.legs_[i - 1U].arr_time_, j.legs_[i].dep_time_);
      EXPECT_EQ(j.legs_[i - 1U].to_, j.legs_[i].from_);
    }
    // single start time: the journey starts at the time of the query
    if (!j.legs_.empty() && dir == direction::kForward) {
      EXPECT_EQ(j.arrival_time(), j.legs_.back().arr_time_);
    } else if (!j.legs_.empty()) {
      EXPECT_EQ(j.departure_time(), j.legs_.front().dep_time_);
    }
    v.push_back(to_str(tt, j));
  }
  std::sort(begin(v), end(v));
  return v;
}

// without the journeys of the extra slot the result is the one of today
void expect_superset(timetable const& tt,
                     std::string_view const from,
                     std::string_view const to,
                     unixtime_t const start_time,
                     direction const dir = direction::kForward) {
  auto with = search(tt, from, to, start_time, true, dir);
  std::erase_if(with, [](std::string const& s) {
    return s.find("slot=1") != std::string::npos;
  });
  EXPECT_EQ(search(tt, from, to, start_time, false, dir), with);
}

// F1 flipping triangle
//   A 10:00 -> B1 10:20     B1 10:25 -> D 10:50
//   A 10:00 -> B2 10:22     B2 10:27 -> D 10:51
constexpr auto const kTriangle = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
AB1,DB,AB1,,,2
AB2,DB,AB2,,,2
B1D,DB,B1D,,,2
B2D,DB,B2D,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
AB1,S,AB1,,
AB2,S,AB2,,
B1D,S,B1D,,
B2D,S,B2D,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
AB1,10:00:00,10:00:00,A,1,0,0
AB1,10:20:00,10:20:00,B1,2,0,0
AB2,10:00:00,10:00:00,A,1,0,0
AB2,10:22:00,10:22:00,B2,2,0,0
B1D,10:25:00,10:25:00,B1,1,0,0
B1D,10:50:00,10:50:00,D,2,0,0
B2D,10:27:00,10:27:00,B2,1,0,0
B2D,10:51:00,10:51:00,D,2,0,0
)";

}  // namespace

TEST(routing, extra_slot_off_is_default) {
  auto const tt = load(kTriangle);
  EXPECT_EQ((strings{"10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1D B1>D "
                     "10:25-10:50"}),
            search(tt, "A", "D", unixtime_t{kDay + 10h}, false));
}

TEST(routing, extra_slot_triangle) {
  auto const tt = load(kTriangle);
  EXPECT_EQ(
      (strings{
          "10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1D B1>D 10:25-10:50",
          "10:51 1 slot=1 | AB2 A>B2 10:00-10:22 | B2D B2>D 10:27-10:51"}),
      search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
  expect_superset(tt, "A", "D", unixtime_t{kDay + 10h});
}

TEST(routing, extra_slot_triangle_backward) {
  auto const tt = load(kTriangle);
  // arrive by 10:51: both leave A at 10:00 -> same time, one slot, best only
  // arrive by 10:50: only the journey via B1
  EXPECT_EQ((strings{"10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1D B1>D "
                     "10:25-10:50"}),
            search(tt, "D", "A", unixtime_t{kDay + 10h + 50min}, true,
                   direction::kBackward));
  expect_superset(tt, "D", "A", unixtime_t{kDay + 10h + 51min},
                  direction::kBackward);
}

namespace {

// F2 same final route, other prefix
//   A 10:00 -> B1 10:20     B1 10:25 -> C 10:40
//   A 10:00 -> B2 10:30     B2 10:35 -> C 10:50
//   t1: C 10:45 -> D 11:10
//   t2: C 10:55 -> D 11:20
// The extra slot at D is t2. The label that takes t2 is the one via B2, the
// label via B1 takes t1.
constexpr auto const kSameFinalRoute = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
C,C,,4.0,5.0,,
D,D,,5.0,6.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
AB1,DB,AB1,,,2
AB2,DB,AB2,,,2
B1C,DB,B1C,,,2
B2C,DB,B2C,,,2
CD,DB,CD,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
AB1,S,AB1,,
AB2,S,AB2,,
B1C,S,B1C,,
B2C,S,B2C,,
CD,S,t1,,
CD,S,t2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
AB1,10:00:00,10:00:00,A,1,0,0
AB1,10:20:00,10:20:00,B1,2,0,0
AB2,10:00:00,10:00:00,A,1,0,0
AB2,10:30:00,10:30:00,B2,2,0,0
B1C,10:25:00,10:25:00,B1,1,0,0
B1C,10:40:00,10:40:00,C,2,0,0
B2C,10:35:00,10:35:00,B2,1,0,0
B2C,10:50:00,10:50:00,C,2,0,0
t1,10:45:00,10:45:00,C,1,0,0
t1,11:10:00,11:10:00,D,2,0,0
t2,10:55:00,10:55:00,C,1,0,0
t2,11:20:00,11:20:00,D,2,0,0
)";

}  // namespace

TEST(routing, extra_slot_same_final_route) {
  auto const tt = load(kSameFinalRoute);
  EXPECT_EQ((strings{"11:10 2 slot=0 | AB1 A>B1 10:00-10:20 | B1C B1>C "
                     "10:25-10:40 | CD C>D 10:45-11:10",
                     "11:20 2 slot=1 | AB2 A>B2 10:00-10:30 | B2C B2>C "
                     "10:35-10:50 | CD C>D 10:55-11:20"}),
            search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
  expect_superset(tt, "A", "D", unixtime_t{kDay + 10h});
}

namespace {

// Extra slot is more than 255 minutes behind: not kept.
//   A 10:00 -> B1 10:20     B1 10:25 -> D 10:50
//   A 10:00 -> B2 10:22     B2 10:27 -> D 15:06   (256 min after 10:50)
//                           B3 10:27 -> D 15:05   (255 min after 10:50)
constexpr auto const kFarBehind = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
B3,B3,,3.5,4.5,,
D,D,,4.0,5.0,,
E,E,,5.0,6.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
AB1,DB,AB1,,,2
AB2,DB,AB2,,,2
AB3,DB,AB3,,,2
B1D,DB,B1D,,,2
B2D,DB,B2D,,,2
B1E,DB,B1E,,,2
B3E,DB,B3E,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
AB1,S,AB1,,
AB2,S,AB2,,
AB3,S,AB3,,
B1D,S,B1D,,
B2D,S,B2D,,
B1E,S,B1E,,
B3E,S,B3E,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
AB1,10:00:00,10:00:00,A,1,0,0
AB1,10:20:00,10:20:00,B1,2,0,0
AB2,10:00:00,10:00:00,A,1,0,0
AB2,10:22:00,10:22:00,B2,2,0,0
AB3,10:00:00,10:00:00,A,1,0,0
AB3,10:22:00,10:22:00,B3,2,0,0
B1D,10:25:00,10:25:00,B1,1,0,0
B1D,10:50:00,10:50:00,D,2,0,0
B2D,10:27:00,10:27:00,B2,1,0,0
B2D,15:06:00,15:06:00,D,2,0,0
B1E,10:25:00,10:25:00,B1,1,0,0
B1E,10:50:00,10:50:00,E,2,0,0
B3E,10:27:00,10:27:00,B3,1,0,0
B3E,15:05:00,15:05:00,E,2,0,0
)";

}  // namespace

TEST(routing, extra_slot_delta_limit) {
  auto const tt = load(kFarBehind);
  EXPECT_EQ((strings{"10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1D B1>D "
                     "10:25-10:50"}),
            search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
  EXPECT_EQ(
      (strings{
          "10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1E B1>E 10:25-10:50",
          "15:05 1 slot=1 | AB3 A>B3 10:00-10:22 | B3E B3>E 10:27-15:05"}),
      search(tt, "A", "E", unixtime_t{kDay + 10h}, true));
}

namespace {

// F6 The extra slot has to beat everything with fewer transfers.
//   direct:   A 10:00 -> D 10:52                              0 transfers
//   via B1:   A 10:00 -> B1 10:20, B1 10:25 -> D 10:50         1 transfer
//   via B2:   A 10:00 -> B2 10:22, B2 10:27 -> D 10:51         1 transfer
//   via B3:   A 10:00 -> B3 10:22, B3 10:27 -> D 10:53         1 transfer
// 10:51 is kept (between 10:50 and 10:52), 10:53 is not. There is a second
// connection without transfer on another route (via X), 10:05 -> 10:58:
// extra slot of round 1. A later trip of the same route would be none, the
// label at A takes the earliest trip of a route.
constexpr auto const kFewerTransfers = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
B3,B3,,3.5,4.5,,
X,X,,3.7,4.7,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
AD,DB,AD,,,2
AD2,DB,AD2,,,2
AB1,DB,AB1,,,2
AB2,DB,AB2,,,2
AB3,DB,AB3,,,2
B1D,DB,B1D,,,2
B2D,DB,B2D,,,2
B3D,DB,B3D,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
AD,S,AD,,
AD2,S,AD2,,
AB1,S,AB1,,
AB2,S,AB2,,
AB3,S,AB3,,
B1D,S,B1D,,
B2D,S,B2D,,
B3D,S,B3D,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
AD,10:00:00,10:00:00,A,1,0,0
AD,10:52:00,10:52:00,D,2,0,0
AD2,10:05:00,10:05:00,A,1,0,0
AD2,10:30:00,10:30:00,X,2,0,0
AD2,10:58:00,10:58:00,D,3,0,0
AB1,10:00:00,10:00:00,A,1,0,0
AB1,10:20:00,10:20:00,B1,2,0,0
AB2,10:00:00,10:00:00,A,1,0,0
AB2,10:22:00,10:22:00,B2,2,0,0
AB3,10:00:00,10:00:00,A,1,0,0
AB3,10:22:00,10:22:00,B3,2,0,0
B1D,10:25:00,10:25:00,B1,1,0,0
B1D,10:50:00,10:50:00,D,2,0,0
B2D,10:27:00,10:27:00,B2,1,0,0
B2D,10:51:00,10:51:00,D,2,0,0
B3D,10:27:00,10:27:00,B3,1,0,0
B3D,10:53:00,10:53:00,D,2,0,0
)";

}  // namespace

TEST(routing, extra_slot_beats_fewer_transfers) {
  auto const tt = load(kFewerTransfers);
  EXPECT_EQ(
      (strings{
          "10:50 1 slot=0 | AB1 A>B1 10:00-10:20 | B1D B1>D 10:25-10:50",
          "10:51 1 slot=1 | AB2 A>B2 10:00-10:22 | B2D B2>D 10:27-10:51",
          "10:52 0 slot=0 | AD A>D 10:00-10:52",
          "10:58 0 slot=1 | AD2 A>D 10:05-10:58"}),
      search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
  expect_superset(tt, "A", "D", unixtime_t{kDay + 10h});
}

namespace {

// Two platforms of the destination station with a footpath between them:
// arriving at D1 and walking to D2 is no second journey.
//   A 10:00 -> D1 10:30
constexpr auto const kDestFootpath = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
D,D,,4.0,5.0,,1,
D1,D1,,4.0,5.0,,0,D
D2,D2,,4.0,5.0,,0,D

# transfers.txt
from_stop_id,to_stop_id,transfer_type,min_transfer_time
D1,D2,2,180
D2,D1,2,180

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
AD,DB,AD,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
AD,S,AD,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
AD,10:00:00,10:00:00,A,1,0,0
AD,10:30:00,10:30:00,D1,2,0,0
)";

}  // namespace

TEST(routing, extra_slot_no_walk_between_destinations) {
  auto const tt = load(kDestFootpath);
  EXPECT_EQ((strings{"10:30 0 slot=0 | AD A>D1 10:00-10:30"}),
            search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
}

namespace {

// The second rider of a route: the best label at B2 cannot take t1 any more,
// it takes t2. t1 is taken at B1.
//   F1: A 10:00 -> B1 10:10
//   F2: A 10:00 -> B2 10:40
//   t1: B1 10:15 -> B2 10:30 -> D 11:00
//   t2: B1 10:45 -> B2 11:00 -> D 11:30
constexpr auto const kSecondRider = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F1,DB,F1,,,2
F2,DB,F2,,,2
T,DB,T,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F1,S,F1,,
F2,S,F2,,
T,S,t1,,
T,S,t2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1,10:00:00,10:00:00,A,1,0,0
F1,10:10:00,10:10:00,B1,2,0,0
F2,10:00:00,10:00:00,A,1,0,0
F2,10:40:00,10:40:00,B2,2,0,0
t1,10:15:00,10:15:00,B1,1,0,0
t1,10:30:00,10:30:00,B2,2,0,0
t1,11:00:00,11:00:00,D,3,0,0
t2,10:45:00,10:45:00,B1,1,0,0
t2,11:00:00,11:00:00,B2,2,0,0
t2,11:30:00,11:30:00,D,3,0,0
)";

}  // namespace

TEST(routing, extra_slot_second_rider) {
  auto const tt = load(kSecondRider);
  // not "F1 A>B1, wait for t2": the label at B1 takes t1
  EXPECT_EQ(
      (strings{"11:00 1 slot=0 | F1 A>B1 10:00-10:10 | T B1>D 10:15-11:00",
               "11:30 1 slot=1 | F2 A>B2 10:00-10:40 | T B2>D 11:00-11:30"}),
      search(tt, "A", "D", unixtime_t{kDay + 10h}, true));
  expect_superset(tt, "A", "D", unixtime_t{kDay + 10h});
}

namespace {

// Two labels at C fall together again: both take T, D has one arrival time.
//   F1: S 10:00 -> C 10:20
//   F2: S 10:05 -> X 10:15 -> C 10:26
//   T : C 10:40 -> D 11:00
// The journey over the later label of C is an alternative with the arrival
// time and transfers of the best journey. Only the tables of the extra slot
// hold the later label.
constexpr auto const kFallTogether = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
X,X,,1.0,2.0,,
C,C,,2.0,3.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F1,DB,F1,,,2
F2,DB,F2,,,2
T,DB,T,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F1,S,F1,,
F2,S,F2,,
T,S,T,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1,10:00:00,10:00:00,S,1,0,0
F1,10:20:00,10:20:00,C,2,0,0
F2,10:05:00,10:05:00,S,1,0,0
F2,10:15:00,10:15:00,X,2,0,0
F2,10:26:00,10:26:00,C,3,0,0
T,10:40:00,10:40:00,C,1,0,0
T,11:00:00,11:00:00,D,2,0,0
)";

strings search_alternatives(timetable const& tt,
                            bool const extra_slot,
                            std::uint8_t const n,
                            bool const tree) {
  auto q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, extra_slot);
  q.n_alternatives_ = n;
  q.alternatives_tree_ = tree;
  auto v = strings{};
  for (auto const& j : raptor_search(tt, nullptr, q)) {
    EXPECT_TRUE(j.is_reconstructed_);
    v.push_back(fmt::format("{} alt={}", to_str(tt, j), j.alternative_));
  }
  return v;
}

}  // namespace

TEST(routing, extra_slot_labels_fall_together) {
  auto const tt = load(kFallTogether);

  constexpr auto const kBest =
      "11:00 1 slot=0 | F1 S>C 10:00-10:20 | T C>D 10:40-11:00 alt=0";
  constexpr auto const kOverLaterLabel =
      "11:00 1 slot=0 | F2 S>C 10:05-10:26 | T C>D 10:40-11:00 alt=1";

  // one arrival time at D: no journey of the extra slot
  EXPECT_EQ((strings{kBest}), search_alternatives(tt, true, 1U, false));

  // the later label of C is not in the tables of today's search
  EXPECT_EQ((strings{kBest}), search_alternatives(tt, false, 2U, false));
  EXPECT_EQ((strings{kBest}), search_alternatives(tt, false, 2U, true));

  // with the extra slot both labels of C lead to D
  EXPECT_EQ((strings{kBest, kOverLaterLabel}),
            search_alternatives(tt, true, 2U, false));
  EXPECT_EQ((strings{kBest, kOverLaterLabel}),
            search_alternatives(tt, true, 2U, true));
}

TEST(routing, extra_slot_alternatives_of_extra_slot_journey) {
  // F2: t2 is taken by the label via B2. No second way to t2 -> the journey
  // of the extra slot has no alternative, the traversal does not fail.
  auto const tt = load(kSameFinalRoute);
  auto q = make_query(tt, "A", "D", unixtime_t{kDay + 10h}, true);
  q.n_alternatives_ = 2U;
  q.alternatives_tree_ = true;
  auto v = strings{};
  for (auto const& j : raptor_search(tt, nullptr, q)) {
    EXPECT_TRUE(j.is_reconstructed_);
    v.push_back(fmt::format("{} alt={}", to_str(tt, j), j.alternative_));
  }
  EXPECT_EQ((strings{"11:10 2 slot=0 | AB1 A>B1 10:00-10:20 | B1C B1>C "
                     "10:25-10:40 | CD C>D 10:45-11:10 alt=0",
                     "11:20 2 slot=1 | AB2 A>B2 10:00-10:30 | B2C B2>C "
                     "10:35-10:50 | CD C>D 10:55-11:20 alt=0"}),
            v);
}
