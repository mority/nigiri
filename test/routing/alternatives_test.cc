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

// Reconstruction with alternatives: query::n_alternatives_ = number of
// options per transit leg of a result entry (start time, dest time,
// transfers). An entry has its journey plus, for each transit leg, up to
// n - 1 journeys that take another option at this leg.
// Journeys of one entry differ in their sequence of trips.
// The first journey of an entry is the one n_alternatives_ = 1 returns.

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

location_idx_t loc_idx(timetable const& tt, std::string_view const id) {
  return tt.find(location_id{id, source_idx_t{0}}).value();
}

routing::query make_query(timetable const& tt,
                          std::string_view const from,
                          std::string_view const to,
                          routing::start_time_t const start_time,
                          std::uint8_t const n) {
  return routing::query{
      .start_time_ = start_time,
      .start_match_mode_ = routing::location_match_mode::kEquivalent,
      .dest_match_mode_ = routing::location_match_mode::kEquivalent,
      .use_start_footpaths_ = false,
      .start_ = {{loc_idx(tt, from), 0min, 0U}},
      .destination_ = {{loc_idx(tt, to), 0min, 0U}},
      .n_alternatives_ = n};
}

// "10:00 11:00 1 | F2 S>B1 | T B1>D"
std::string to_str(timetable const& tt, routing::journey const& j) {
  auto s = fmt::format("{} {} {}", date::format("%H:%M", j.departure_time()),
                       date::format("%H:%M", j.arrival_time()), j.transfers_);
  for (auto const& l : j.legs_) {
    if (std::holds_alternative<routing::journey::run_enter_exit>(l.uses_)) {
      auto const& r = std::get<routing::journey::run_enter_exit>(l.uses_).r_;
      s += fmt::format(" | {} {}>{}", tt.transport_name(r.t_.t_idx_),
                       tt.locations_.ids_[l.from_].view(),
                       tt.locations_.ids_[l.to_].view());
    }
  }
  return s;
}

std::vector<std::string> to_str(timetable const& tt,
                                pareto_set<routing::journey> const& results) {
  auto v = std::vector<std::string>{};
  for (auto const& j : results) {
    v.push_back(to_str(tt, j));
  }
  return v;
}

std::vector<std::string> sorted(std::vector<std::string> v) {
  std::sort(begin(v), end(v));
  return v;
}

using strings = std::vector<std::string>;

// n = 1 returns one journey per entry, n > 1 starts each entry with it
void expect_first_is_default(timetable const& tt,
                             routing::query q,
                             direction const dir = direction::kForward) {
  auto const n = raptor_search(tt, nullptr, q, dir);
  q.n_alternatives_ = 1U;
  auto const one = raptor_search(tt, nullptr, q, dir);

  auto first = strings{};
  for (auto const& j : n) {
    EXPECT_TRUE(j.is_reconstructed_);
    EXPECT_FALSE(j.error_);
    if (j.alternative_ == 0U) {
      first.push_back(to_str(tt, j));
    }
  }
  EXPECT_EQ(to_str(tt, one), first);
  for (auto const& j : one) {
    EXPECT_EQ(0U, j.alternative_);
  }
}

// The last trip T can be entered at B1 and at B2, one feeder each.
//   F1: S 10:00 -> B2 10:20
//   F2: S 10:00 -> B1 10:15
//   F3: S 10:00 -> B0 10:05
//   T : B0 10:20 -> B1 10:30 -> B2 10:40 -> D 11:00
constexpr auto const kEntryStops = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
B0,B0,,1.0,2.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F1,DB,F1,,,2
F2,DB,F2,,,2
F3,DB,F3,,,2
T,DB,T,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F1,S,F1,,
F2,S,F2,,
F3,S,F3,,
T,S,T,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1,10:00:00,10:00:00,S,1,0,0
F1,10:20:00,10:20:00,B2,2,0,0
F2,10:00:00,10:00:00,S,1,0,0
F2,10:15:00,10:15:00,B1,2,0,0
F3,10:00:00,10:00:00,S,1,0,0
F3,10:05:00,10:05:00,B0,2,0,0
T,10:20:00,10:20:00,B0,1,0,0
T,10:30:00,10:30:00,B1,2,0,0
T,10:40:00,10:40:00,B2,3,0,0
T,11:00:00,11:00:00,D,4,0,0
)";

}  // namespace

TEST(routing, alternatives_n1_is_default) {
  auto const tt = load(kEntryStops);
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 1U);
  auto const results = raptor_search(tt, nullptr, q);
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D"}), to_str(tt, results));
  EXPECT_EQ(0U, begin(results)->alternative_);
}

TEST(routing, alternatives_other_entry_stop) {
  auto const tt = load(kEntryStops);
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);

  // entry stops are visited from the exit stop backwards -> B2, then B1
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D"}),
            to_str(tt, results));
  ASSERT_EQ(2U, results.size());
  EXPECT_EQ(0U, results.els_[0].alternative_);
  EXPECT_EQ(1U, results.els_[1].alternative_);
  expect_first_is_default(tt, q);
}

TEST(routing, alternatives_n_limits_options_per_leg) {
  auto const tt = load(kEntryStops);

  auto const three = raptor_search(
      tt, nullptr, make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 3U));
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D",
                     "10:00 11:00 1 | F3 S>B0 | T B0>D"}),
            to_str(tt, three));

  // more asked for than there are
  auto const four = raptor_search(
      tt, nullptr, make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 4U));
  EXPECT_EQ(to_str(tt, three), to_str(tt, four));
}

TEST(routing, alternatives_interval) {
  auto const tt = load(kEntryStops);
  auto const q =
      make_query(tt, "S", "D",
                 interval<unixtime_t>{kDay + 9h + 30min, kDay + 10h + 1min}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D"}),
            to_str(tt, results));
  expect_first_is_default(tt, q);
}

namespace {

// Two start times, every entry has its own alternative. The labels of the
// later start time are still in the tables when the earlier one is
// reconstructed: a journey of 09:30 must not continue on a label of 10:00.
//   F1 : S 09:30 -> B2 09:50      S 10:00 -> B2 10:20
//   F2 : S 09:30 -> B1 09:45      S 10:00 -> B1 10:15
//   T  : B1 10:00 -> B2 10:10 -> D 10:30
//        B1 10:30 -> B2 10:40 -> D 11:00
constexpr auto const kTwoStartTimes = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
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
F1,S,F1a,,
F1,S,F1b,,
F2,S,F2a,,
F2,S,F2b,,
T,S,Ta,,
T,S,Tb,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1a,09:30:00,09:30:00,S,1,0,0
F1a,09:50:00,09:50:00,B2,2,0,0
F1b,10:00:00,10:00:00,S,1,0,0
F1b,10:20:00,10:20:00,B2,2,0,0
F2a,09:30:00,09:30:00,S,1,0,0
F2a,09:45:00,09:45:00,B1,2,0,0
F2b,10:00:00,10:00:00,S,1,0,0
F2b,10:15:00,10:15:00,B1,2,0,0
Ta,10:00:00,10:00:00,B1,1,0,0
Ta,10:10:00,10:10:00,B2,2,0,0
Ta,10:30:00,10:30:00,D,3,0,0
Tb,10:30:00,10:30:00,B1,1,0,0
Tb,10:40:00,10:40:00,B2,2,0,0
Tb,11:00:00,11:00:00,D,3,0,0
)";

}  // namespace

TEST(routing, alternatives_two_start_times) {
  auto const tt = load(kTwoStartTimes);
  auto const q = make_query(
      tt, "S", "D", interval<unixtime_t>{kDay + 9h, kDay + 10h + 1min}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  EXPECT_EQ((strings{"09:30 10:30 1 | F1 S>B2 | T B2>D",
                     "09:30 10:30 1 | F2 S>B1 | T B1>D",
                     "10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D"}),
            to_str(tt, results));
  for (auto const& j : results) {
    EXPECT_EQ(j.departure_time(), j.legs_.front().dep_time_);
    EXPECT_EQ(j.arrival_time(), j.legs_.back().arr_time_);
  }
  expect_first_is_default(tt, q);
}

namespace {

// Two trips reach the transfer stop C in the same minute, two trips reach
// the destination in the same minute.
//   F1: S 10:00 -> C 10:20
//   F2: S 10:05 -> X 10:10 -> C 10:20
//   U1: C 10:40 -> D 11:00
//   U2: C 10:45 -> Y 10:50 -> D 11:00
constexpr auto const kSameMinute = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
X,X,,1.0,2.0,,
C,C,,2.0,3.0,,
Y,Y,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F1,DB,F1,,,2
F2,DB,F2,,,2
U1,DB,U1,,,2
U2,DB,U2,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F1,S,F1,,
F2,S,F2,,
U1,S,U1,,
U2,S,U2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1,10:00:00,10:00:00,S,1,0,0
F1,10:20:00,10:20:00,C,2,0,0
F2,10:05:00,10:05:00,S,1,0,0
F2,10:10:00,10:10:00,X,2,0,0
F2,10:20:00,10:20:00,C,3,0,0
U1,10:40:00,10:40:00,C,1,0,0
U1,11:00:00,11:00:00,D,2,0,0
U2,10:45:00,10:45:00,C,1,0,0
U2,10:50:00,10:50:00,Y,2,0,0
U2,11:00:00,11:00:00,D,3,0,0
)";

}  // namespace

TEST(routing, alternatives_same_minute_other_trip) {
  auto const tt = load(kSameMinute);

  // Two options at each of the two legs: the journey, one journey with the
  // other last trip, one journey with the other first trip. The journey
  // that takes the other option at both legs is not part of the result.
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  ASSERT_EQ(3U, results.size());

  auto const trips = [&](routing::journey const& j) {
    auto const s = to_str(tt, j);
    auto const first = s.substr(s.find('|') + 2U, 2U);
    auto const last = s.substr(s.rfind('|') + 2U, 2U);
    return std::pair{first, last};
  };
  auto const [first, last] = trips(results.els_[0]);
  auto const other = [](std::string const& x) {
    return std::string{x[0], x[1] == '1' ? '2' : '1'};
  };
  // leg next to the destination first
  EXPECT_EQ((std::pair{first, other(last)}), trips(results.els_[1]));
  EXPECT_EQ((std::pair{other(first), last}), trips(results.els_[2]));
  EXPECT_EQ(0U, results.els_[0].alternative_);
  EXPECT_EQ(1U, results.els_[1].alternative_);
  EXPECT_EQ(2U, results.els_[2].alternative_);
  expect_first_is_default(tt, q);

  // more options asked for than there are
  auto const four = raptor_search(
      tt, nullptr, make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 4U));
  EXPECT_EQ(to_str(tt, results), to_str(tt, four));
}

namespace {

// Only the last leg has a choice: the search for the second journey has to
// step back from the leg next to the start.
//   F : S 10:00 -> C 10:20
//   U1: C 10:40 -> D 11:00
//   U2: C 10:45 -> Y 10:50 -> D 11:00
constexpr auto const kChoiceAtLastLeg = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
C,C,,2.0,3.0,,
Y,Y,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F,DB,F,,,2
U1,DB,U1,,,2
U2,DB,U2,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F,S,F,,
U1,S,U1,,
U2,S,U2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F,10:00:00,10:00:00,S,1,0,0
F,10:20:00,10:20:00,C,2,0,0
U1,10:40:00,10:40:00,C,1,0,0
U1,11:00:00,11:00:00,D,2,0,0
U2,10:45:00,10:45:00,C,1,0,0
U2,10:50:00,10:50:00,Y,2,0,0
U2,11:00:00,11:00:00,D,3,0,0
)";

}  // namespace

TEST(routing, alternatives_choice_at_last_leg) {
  auto const tt = load(kChoiceAtLastLeg);
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  EXPECT_EQ((strings{"10:00 11:00 1 | F S>C | U1 C>D",
                     "10:00 11:00 1 | F S>C | U2 C>D"}),
            sorted(to_str(tt, results)));
  expect_first_is_default(tt, q);
}

namespace {

// Same trips, other stops: F and T both serve B1 and B2, the transfer is
// possible at both. One sequence of trips -> one journey.
//   F: S 10:00 -> B1 10:10 -> B2 10:20
//   T: B1 10:30 -> B2 10:40 -> D 11:00
constexpr auto const kSameTrips = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F,DB,F,,,2
T,DB,T,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F,S,F,,
T,S,T,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F,10:00:00,10:00:00,S,1,0,0
F,10:10:00,10:10:00,B1,2,0,0
F,10:20:00,10:20:00,B2,3,0,0
T,10:30:00,10:30:00,B1,1,0,0
T,10:40:00,10:40:00,B2,2,0,0
T,11:00:00,11:00:00,D,3,0,0
)";

}  // namespace

TEST(routing, alternatives_same_trips_is_no_alternative) {
  auto const tt = load(kSameTrips);
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  EXPECT_EQ((strings{"10:00 11:00 1 | F S>B2 | T B2>D"}), to_str(tt, results));
  expect_first_is_default(tt, q);
}

namespace {

// Three legs, the choice is in the middle: T can be entered at B1 and B2.
//   F1: S 10:00 -> B2 10:20
//   F2: S 10:00 -> B1 10:15
//   T : B1 10:30 -> B2 10:40 -> C 11:00
//   U : C 11:10 -> D 11:30
// Pareto set has a second entry: direct, slower.
//   X : S 10:00 -> D 12:00
constexpr auto const kThreeLegs = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
B1,B1,,2.0,3.0,,
B2,B2,,3.0,4.0,,
C,C,,4.0,5.0,,
D,D,,5.0,6.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F1,DB,F1,,,2
F2,DB,F2,,,2
T,DB,T,,,2
U,DB,U,,,2
X,DB,X,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F1,S,F1,,
F2,S,F2,,
T,S,T,,
U,S,U,,
X,S,X,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F1,10:00:00,10:00:00,S,1,0,0
F1,10:20:00,10:20:00,B2,2,0,0
F2,10:00:00,10:00:00,S,1,0,0
F2,10:15:00,10:15:00,B1,2,0,0
T,10:30:00,10:30:00,B1,1,0,0
T,10:40:00,10:40:00,B2,2,0,0
T,11:00:00,11:00:00,C,3,0,0
U,11:10:00,11:10:00,C,1,0,0
U,11:30:00,11:30:00,D,2,0,0
X,10:00:00,10:00:00,S,1,0,0
X,12:00:00,12:00:00,D,2,0,0
)";

}  // namespace

TEST(routing, alternatives_per_entry) {
  auto const tt = load(kThreeLegs);
  auto const q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);

  // alternatives follow their entry, the direct entry has none
  EXPECT_EQ((strings{"10:00 12:00 0 | X S>D",
                     "10:00 11:30 2 | F1 S>B2 | T B2>C | U C>D",
                     "10:00 11:30 2 | F2 S>B1 | T B1>C | U C>D"}),
            to_str(tt, results));
  ASSERT_EQ(3U, results.size());
  EXPECT_EQ(0U, results.els_[0].alternative_);
  EXPECT_EQ(0U, results.els_[1].alternative_);
  EXPECT_EQ(1U, results.els_[2].alternative_);
  expect_first_is_default(tt, q);
}

TEST(routing, alternatives_no_alternative) {
  auto const tt = load(kThreeLegs);
  auto const q = make_query(tt, "B1", "D", unixtime_t{kDay + 10h}, 2U);
  auto const results = raptor_search(tt, nullptr, q);
  // ontrip: the journey starts at the start time of the query
  EXPECT_EQ((strings{"10:00 11:30 1 | T B1>C | U C>D"}), to_str(tt, results));
  expect_first_is_default(tt, q);
}

namespace {

// Backward: the first trip T can be left at C1 and at C2.
//   T : S 10:00 -> C1 10:20 -> C2 10:30
//   G1: C1 10:30 -> D 11:00
//   G2: C2 10:40 -> D 11:00
constexpr auto const kBackward = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,
C1,C1,,2.0,3.0,,
C2,C2,,3.0,4.0,,
D,D,,4.0,5.0,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
T,DB,T,,,2
G1,DB,G1,,,2
G2,DB,G2,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
T,S,T,,
G1,S,G1,,
G2,S,G2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T,10:00:00,10:00:00,S,1,0,0
T,10:20:00,10:20:00,C1,2,0,0
T,10:30:00,10:30:00,C2,3,0,0
G1,10:30:00,10:30:00,C1,1,0,0
G1,11:00:00,11:00:00,D,2,0,0
G2,10:40:00,10:40:00,C2,1,0,0
G2,11:00:00,11:00:00,D,2,0,0
)";

}  // namespace

TEST(routing, alternatives_backward) {
  auto const tt = load(kBackward);
  // backward: start of the search = D at 11:00, destination = S
  auto const q = make_query(tt, "D", "S", unixtime_t{kDay + 11h}, 2U);
  auto const results = raptor_search(tt, nullptr, q, direction::kBackward);
  EXPECT_EQ((strings{"10:00 11:00 1 | T S>C1 | G1 C1>D",
                     "10:00 11:00 1 | T S>C2 | G2 C2>D"}),
            sorted(to_str(tt, results)));
  expect_first_is_default(tt, q, direction::kBackward);
}

TEST(routing, alternatives_tree) {
  auto const tt = load(kSameMinute);

  // two options at each of the two legs -> 2 x 2 journeys
  auto q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
  q.alternatives_tree_ = true;
  auto const results = raptor_search(tt, nullptr, q);
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>C | U1 C>D",
                     "10:00 11:00 1 | F1 S>C | U2 C>D",
                     "10:00 11:00 1 | F2 S>C | U1 C>D",
                     "10:00 11:00 1 | F2 S>C | U2 C>D"}),
            sorted(to_str(tt, results)));
  expect_first_is_default(tt, q);

  // three entry stops of the last trip, n = 2 looks at two of them
  auto const tt1 = load(kEntryStops);
  auto q1 = make_query(tt1, "S", "D", unixtime_t{kDay + 10h}, 2U);
  q1.alternatives_tree_ = true;
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D"}),
            to_str(tt1, raptor_search(tt1, nullptr, q1)));

  // same trips stay one journey
  auto const tt2 = load(kSameTrips);
  auto q2 = make_query(tt2, "S", "D", unixtime_t{kDay + 10h}, 3U);
  q2.alternatives_tree_ = true;
  EXPECT_EQ((strings{"10:00 11:00 1 | F S>B2 | T B2>D"}),
            to_str(tt2, raptor_search(tt2, nullptr, q2)));
}

TEST(routing, alternatives_tree_unlimited) {
  // every edge of every label
  auto const tt = load(kEntryStops);
  auto q = make_query(tt, "S", "D", unixtime_t{kDay + 10h},
                      routing::kUnlimitedAlternatives);
  q.alternatives_tree_ = true;
  EXPECT_EQ((strings{"10:00 11:00 1 | F1 S>B2 | T B2>D",
                     "10:00 11:00 1 | F2 S>B1 | T B1>D",
                     "10:00 11:00 1 | F3 S>B0 | T B0>D"}),
            to_str(tt, raptor_search(tt, nullptr, q)));

  auto const tt1 = load(kSameMinute);
  auto q1 = make_query(tt1, "S", "D", unixtime_t{kDay + 10h},
                       routing::kUnlimitedAlternatives);
  q1.alternatives_tree_ = true;
  EXPECT_EQ(4U, raptor_search(tt1, nullptr, q1).size());
}

namespace {

// Two platforms of the destination station, reached in the same minute by
// different trips.
//   F : S 10:00 -> C 10:20
//   U1: C 10:40 -> D1 11:00
//   U2: C 10:45 -> Y 10:50 -> D2 11:00
constexpr auto const kTwoPlatforms = R"(
# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S,S,,0.0,1.0,,,
C,C,,2.0,3.0,,,
Y,Y,,3.0,4.0,,,
D,D,,4.0,5.0,,1,
D1,D1,,4.0,5.0,,0,D
D2,D2,,4.0,5.0,,0,D

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
F,DB,F,,,2
U1,DB,U1,,,2
U2,DB,U2,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
F,S,F,,
U1,S,U1,,
U2,S,U2,,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
F,10:00:00,10:00:00,S,1,0,0
F,10:20:00,10:20:00,C,2,0,0
U1,10:40:00,10:40:00,C,1,0,0
U1,11:00:00,11:00:00,D1,2,0,0
U2,10:45:00,10:45:00,C,1,0,0
U2,10:50:00,10:50:00,Y,2,0,0
U2,11:00:00,11:00:00,D2,3,0,0
)";

}  // namespace

TEST(routing, alternatives_other_stop_of_destination) {
  auto const tt = load(kTwoPlatforms);
  for (auto const tree : {false, true}) {
    auto q = make_query(tt, "S", "D", unixtime_t{kDay + 10h}, 2U);
    q.alternatives_tree_ = tree;
    EXPECT_EQ((strings{"10:00 11:00 1 | F S>C | U1 C>D1",
                       "10:00 11:00 1 | F S>C | U2 C>D2"}),
              sorted(to_str(tt, raptor_search(tt, nullptr, q))));
    expect_first_is_default(tt, q);
  }
}
