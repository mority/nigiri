#include "gtest/gtest.h"

#include <set>
#include <tuple>

#include "nigiri/loader/dir.h"
#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/special_stations.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::loader::gtfs;
using namespace std::chrono_literals;

namespace {

// A --R1--> C hourly (2 h, 0 transfers) plus one early trip 09:30 -> 11:30,
// A --R2--> B --R3--> C hourly (dep xx:15, arr (xx+1):20, 1 transfer).
// Neither family dominates the other, so the profile interleaves them.
mem_dir test_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Etc/UTC

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B,B,,2.0,3.0,,
C,C,,4.0,5.0,,

# calendar_dates.txt
service_id,date,exception_type
S,20240619,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,RE 1,,,2
R2,DB,RE 2,,,2
R3,DB,RE 3,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S,T1_09,RE 1,
R1,S,T1_10,RE 1,
R1,S,T1_11,RE 1,
R1,S,T1_12,RE 1,
R1,S,T1_13,RE 1,
R1,S,T1_14,RE 1,
R1,S,T1_15,RE 1,
R2,S,T2_10,RE 2,
R2,S,T2_11,RE 2,
R2,S,T2_12,RE 2,
R2,S,T2_13,RE 2,
R2,S,T2_14,RE 2,
R2,S,T2_15,RE 2,
R3,S,T3_10,RE 3,
R3,S,T3_11,RE 3,
R3,S,T3_12,RE 3,
R3,S,T3_13,RE 3,
R3,S,T3_14,RE 3,
R3,S,T3_15,RE 3,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1_09,09:30:00,09:30:00,A,1,0,0
T1_09,11:30:00,11:30:00,C,2,0,0
T1_10,10:00:00,10:00:00,A,1,0,0
T1_10,12:00:00,12:00:00,C,2,0,0
T1_11,11:00:00,11:00:00,A,1,0,0
T1_11,13:00:00,13:00:00,C,2,0,0
T1_12,12:00:00,12:00:00,A,1,0,0
T1_12,14:00:00,14:00:00,C,2,0,0
T1_13,13:00:00,13:00:00,A,1,0,0
T1_13,15:00:00,15:00:00,C,2,0,0
T1_14,14:00:00,14:00:00,A,1,0,0
T1_14,16:00:00,16:00:00,C,2,0,0
T1_15,15:00:00,15:00:00,A,1,0,0
T1_15,17:00:00,17:00:00,C,2,0,0
T2_10,10:15:00,10:15:00,A,1,0,0
T2_10,10:45:00,10:45:00,B,2,0,0
T2_11,11:15:00,11:15:00,A,1,0,0
T2_11,11:45:00,11:45:00,B,2,0,0
T2_12,12:15:00,12:15:00,A,1,0,0
T2_12,12:45:00,12:45:00,B,2,0,0
T2_13,13:15:00,13:15:00,A,1,0,0
T2_13,13:45:00,13:45:00,B,2,0,0
T2_14,14:15:00,14:15:00,A,1,0,0
T2_14,14:45:00,14:45:00,B,2,0,0
T2_15,15:15:00,15:15:00,A,1,0,0
T2_15,15:45:00,15:45:00,B,2,0,0
T3_10,10:50:00,10:50:00,B,1,0,0
T3_10,11:20:00,11:20:00,C,2,0,0
T3_11,11:50:00,11:50:00,B,1,0,0
T3_11,12:20:00,12:20:00,C,2,0,0
T3_12,12:50:00,12:50:00,B,1,0,0
T3_12,13:20:00,13:20:00,C,2,0,0
T3_13,13:50:00,13:50:00,B,1,0,0
T3_13,14:20:00,14:20:00,C,2,0,0
T3_14,14:50:00,14:50:00,B,1,0,0
T3_14,15:20:00,15:20:00,C,2,0,0
T3_15,15:50:00,15:50:00,B,1,0,0
T3_15,16:20:00,16:20:00,C,2,0,0
)");
}

// Same stops, but direct trains only every two hours (12:00, 14:00, 16:00)
// plus the early one, so that the first page by departure and the first
// page by arrival differ: 11:55 -> 14:03 (0) is the third departure after
// 10:00 but only the fourth arrival.
mem_dir sparse_direct_files() {
  return mem_dir::read(R"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
DB,Deutsche Bahn,https://deutschebahn.com,Etc/UTC

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,0.0,1.0,,
B,B,,2.0,3.0,,
C,C,,4.0,5.0,,

# calendar_dates.txt
service_id,date,exception_type
S,20240619,1

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,DB,RE 1,,,2
R2,DB,RE 2,,,2
R3,DB,RE 3,,,2

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,S,T1_09,RE 1,
R1,S,T1_12,RE 1,
R1,S,T1_14,RE 1,
R1,S,T1_16,RE 1,
R2,S,T2_10,RE 2,
R2,S,T2_11,RE 2,
R2,S,T2_12,RE 2,
R2,S,T2_13,RE 2,
R2,S,T2_14,RE 2,
R2,S,T2_15,RE 2,
R3,S,T3_10,RE 3,
R3,S,T3_11,RE 3,
R3,S,T3_12,RE 3,
R3,S,T3_13,RE 3,
R3,S,T3_14,RE 3,
R3,S,T3_15,RE 3,

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
T1_09,09:30:00,09:30:00,A,1,0,0
T1_09,11:30:00,11:30:00,C,2,0,0
T1_12,12:00:00,12:00:00,A,1,0,0
T1_12,14:00:00,14:00:00,C,2,0,0
T1_14,14:00:00,14:00:00,A,1,0,0
T1_14,16:00:00,16:00:00,C,2,0,0
T1_16,16:00:00,16:00:00,A,1,0,0
T1_16,18:00:00,18:00:00,C,2,0,0
T2_10,10:15:00,10:15:00,A,1,0,0
T2_10,10:45:00,10:45:00,B,2,0,0
T2_11,11:15:00,11:15:00,A,1,0,0
T2_11,11:45:00,11:45:00,B,2,0,0
T2_12,12:15:00,12:15:00,A,1,0,0
T2_12,12:45:00,12:45:00,B,2,0,0
T2_13,13:15:00,13:15:00,A,1,0,0
T2_13,13:45:00,13:45:00,B,2,0,0
T2_14,14:15:00,14:15:00,A,1,0,0
T2_14,14:45:00,14:45:00,B,2,0,0
T2_15,15:15:00,15:15:00,A,1,0,0
T2_15,15:45:00,15:45:00,B,2,0,0
T3_10,10:50:00,10:50:00,B,1,0,0
T3_10,11:20:00,11:20:00,C,2,0,0
T3_11,11:50:00,11:50:00,B,1,0,0
T3_11,12:20:00,12:20:00,C,2,0,0
T3_12,12:50:00,12:50:00,B,1,0,0
T3_12,13:20:00,13:20:00,C,2,0,0
T3_13,13:50:00,13:50:00,B,1,0,0
T3_13,14:20:00,14:20:00,C,2,0,0
T3_14,14:50:00,14:50:00,B,1,0,0
T3_14,15:20:00,15:20:00,C,2,0,0
T3_15,15:50:00,15:50:00,B,1,0,0
T3_15,16:20:00,16:20:00,C,2,0,0
)");
}

using jkey_t = std::tuple<unixtime_t, unixtime_t, std::uint8_t>;

std::set<jkey_t> keys(pareto_set<routing::journey> const& js) {
  auto s = std::set<jkey_t>{};
  for (auto const& j : js) {
    s.emplace(j.departure_time(), j.arrival_time(), j.transfers_);
  }
  return s;
}

std::string str(std::set<jkey_t> const& s) {
  auto out = std::string{};
  for (auto const& [dep, arr, t] : s) {
    out += fmt::format("{} -> {} ({})\n", dep, arr, t);
  }
  return out;
}

struct fixture {
  explicit fixture(bool const sparse_direct = false) {
    tt_.date_range_ = {sys_days{2024_y / June / 18},
                       sys_days{2024_y / June / 20}};
    register_special_stations(tt_);
    load_timetable({}, source_idx_t{0},
                   sparse_direct ? sparse_direct_files() : test_files(), tt_);
    finalize(tt_);
    a_ = tt_.find(location_id{"A", source_idx_t{0}}).value();
    c_ = tt_.find(location_id{"C", source_idx_t{0}}).value();
  }

  // The complete profile of journeys leaving A in [10:00, 18:00): a forward
  // range query over the fixed window, no extension.
  routing::query full_profile_query() const {
    return routing::query{
        .start_time_ = interval<unixtime_t>{day_ + 10h, day_ + 18h},
        .start_match_mode_ = routing::location_match_mode::kIntermodal,
        .dest_match_mode_ = routing::location_match_mode::kIntermodal,
        .use_start_footpaths_ = false,
        .start_ = {{a_, 5min, 0U}},
        .destination_ = {{c_, 3min, 0U}},
        .min_connection_count_ = 0U};
  }

  // The first n journeys leaving A at or after 10:00, open end: what motis
  // sends for numItineraries=n, searchWindow=0.
  routing::query first_n_query(unsigned const n) const {
    return routing::query{
        .start_time_ = interval<unixtime_t>{day_ + 10h, day_ + 10h},
        .start_match_mode_ = routing::location_match_mode::kIntermodal,
        .dest_match_mode_ = routing::location_match_mode::kIntermodal,
        .use_start_footpaths_ = false,
        .start_ = {{a_, 5min, 0U}},
        .destination_ = {{c_, 3min, 0U}},
        .min_connection_count_ = n,
        .extend_interval_later_ = true};
  }

  // Arrive-by: the n latest arriving journeys at or before 16:00, open
  // earlier end, the query already flipped as motis does for arriveBy
  // (start = the destination side, interval [t, t + 1 min)).
  routing::query last_n_by_query(unsigned const n) const {
    return routing::query{
        .start_time_ = interval<unixtime_t>{day_ + 16h, day_ + 16h + 1min},
        .start_match_mode_ = routing::location_match_mode::kIntermodal,
        .dest_match_mode_ = routing::location_match_mode::kIntermodal,
        .use_start_footpaths_ = false,
        .start_ = {{c_, 3min, 0U}},
        .destination_ = {{a_, 5min, 0U}},
        .min_connection_count_ = n,
        .extend_interval_earlier_ = true};
  }

  timetable tt_;
  location_idx_t a_, c_;
  sys_days day_{2024_y / June / 19};
};

std::vector<jkey_t> by_departure(std::set<jkey_t> const& p) {
  auto v = std::vector<jkey_t>{begin(p), end(p)};
  std::sort(begin(v), end(v), [](auto const& a, auto const& b) {
    return std::tuple{std::get<0>(a), std::get<2>(a), std::get<1>(a)} <
           std::tuple{std::get<0>(b), std::get<2>(b), std::get<1>(b)};
  });
  return v;
}

}  // namespace

TEST(routing, reversed_search_first_n_by_arrival) {
  auto f = fixture{};
  auto const profile =
      keys(test::raptor_search(f.tt_, nullptr, f.full_profile_query()));
  // 10:00 and 09:30 trains need leaving A before 10:00 (5 min offset): out.
  ASSERT_EQ(11U, profile.size()) << str(profile);

  auto const n = 3U;
  auto const results =
      test::search_reversed(f.tt_, nullptr, f.first_n_query(n));
  auto const got = keys(results);

  // Every reported journey is part of the profile ...
  for (auto const& k : got) {
    EXPECT_TRUE(profile.contains(k))
        << "not in profile: " << str({k}) << "profile:\n"
        << str(profile);
  }
  // ... and the reported journeys are exactly the n earliest arriving ones.
  auto by_arrival = std::vector<jkey_t>{begin(profile), end(profile)};
  std::sort(begin(by_arrival), end(by_arrival), [](auto const& a, auto const& b) {
    return std::tuple{std::get<1>(a), std::get<2>(a), std::get<0>(a)} <
           std::tuple{std::get<1>(b), std::get<2>(b), std::get<0>(b)};
  });
  auto const expected = std::set<jkey_t>{begin(by_arrival), begin(by_arrival) + n};
  EXPECT_EQ(expected, got) << "expected:\n" << str(expected) << "got:\n"
                           << str(got);
  // 10:10 -> 11:23 (1), 11:10 -> 12:23 (1), 10:55 -> 13:03 (0); the 0-transfer
  // trains arriving 11:33 and 12:03 leave A before 10:00 and must not appear.
  EXPECT_EQ((std::set<jkey_t>{{f.day_ + 10h + 10min, f.day_ + 11h + 23min, 1U},
                             {f.day_ + 11h + 10min, f.day_ + 12h + 23min, 1U},
                             {f.day_ + 10h + 55min, f.day_ + 13h + 3min, 0U}}),
            got)
      << str(got);

  // Orientation of the query: sorted by departure, intermodal markers in
  // place, legs reconstructed.
  auto prev = unixtime_t::min();
  for (auto const& j : results) {
    EXPECT_LE(prev, j.start_time_);
    prev = j.start_time_;
    EXPECT_LE(j.start_time_, j.dest_time_);
    ASSERT_FALSE(j.legs_.empty());
    EXPECT_EQ(get_special_station(special_station::kStart),
              j.legs_.front().from_);
    EXPECT_EQ(get_special_station(special_station::kEnd), j.legs_.back().to_);
    EXPECT_EQ(j.start_time_, j.legs_.front().dep_time_);
    EXPECT_EQ(j.dest_time_, j.legs_.back().arr_time_);
  }
}

TEST(routing, reversed_search_exhausts_profile) {
  auto f = fixture{};
  auto const profile =
      keys(test::raptor_search(f.tt_, nullptr, f.full_profile_query()));
  // More journeys requested than exist: the scan extends until the day is
  // exhausted and reports the whole profile.
  auto const got =
      keys(test::search_reversed(f.tt_, nullptr, f.first_n_query(20U)));
  EXPECT_EQ(profile, got) << "profile:\n" << str(profile) << "got:\n"
                          << str(got);
}

TEST(routing, reversed_search_matches_range_query_for_first_n) {
  auto f = fixture{};
  // The forward range query with the same open-ended request. Its answer is
  // every journey departing inside the interval it ended up searching; the
  // reversed search's answer is the n earliest arriving journeys. The latter
  // must be a subset of the former whenever the range query's interval
  // reaches far enough, i.e. the reversed search never invents journeys.
  auto const rq = keys(test::raptor_search(f.tt_, nullptr, f.first_n_query(5U)));
  auto const rev = keys(test::search_reversed(f.tt_, nullptr, f.first_n_query(5U)));
  EXPECT_EQ(5U, rev.size()) << str(rev);
  auto const profile =
      keys(test::raptor_search(f.tt_, nullptr, f.full_profile_query()));
  for (auto const& k : rev) {
    EXPECT_TRUE(profile.contains(k)) << str({k});
  }
  for (auto const& k : rq) {
    EXPECT_TRUE(profile.contains(k)) << str({k});
  }
}

TEST(routing, reversed_search_departure_order_first_n) {
  auto f = fixture{true};
  auto const profile =
      keys(test::raptor_search(f.tt_, nullptr, f.full_profile_query()));
  ASSERT_EQ(9U, profile.size()) << str(profile);
  auto const dep = by_departure(profile);

  using routing::reversed_order;
  auto const n = 3U;
  auto const by_arr = keys(test::search_reversed(
      f.tt_, nullptr, f.first_n_query(n), direction::kForward,
      reversed_order::kArrival));
  auto const by_dep = keys(test::search_reversed(
      f.tt_, nullptr, f.first_n_query(n), direction::kForward,
      reversed_order::kDeparture));

  // the two orders answer different questions on this timetable
  EXPECT_EQ((std::set<jkey_t>{{f.day_ + 10h + 10min, f.day_ + 11h + 23min, 1U},
                              {f.day_ + 11h + 10min, f.day_ + 12h + 23min, 1U},
                              {f.day_ + 12h + 10min, f.day_ + 13h + 23min, 1U}}),
            by_arr)
      << str(by_arr);
  EXPECT_EQ((std::set<jkey_t>{{f.day_ + 10h + 10min, f.day_ + 11h + 23min, 1U},
                              {f.day_ + 11h + 10min, f.day_ + 12h + 23min, 1U},
                              {f.day_ + 11h + 55min, f.day_ + 14h + 3min, 0U}}),
            by_dep)
      << str(by_dep);
  EXPECT_EQ((std::set<jkey_t>{begin(dep), begin(dep) + n}), by_dep);

  // n = 4 needs a fill: the 0-transfer frontier (11:55) lies before the
  // candidate 12:10 -> 13:23 (1), so a forward/backward pair from 11:56
  // must prove that no direct train departs in between
  for (auto const k : {4U, 5U, 7U}) {
    auto const got = keys(test::search_reversed(
        f.tt_, nullptr, f.first_n_query(k), direction::kForward,
        reversed_order::kDeparture));
    EXPECT_EQ((std::set<jkey_t>{begin(dep), begin(dep) + k}), got)
        << "n=" << k << "\n" << str(got);
  }

  // the fill path: n = 3 needs only the seed (one forward search), n = 4
  // one more forward/backward pair from 11:56
  auto const n_forward = [&](unsigned const k) {
    auto search_state = routing::search_state{};
    auto algo_state = routing::raptor_state{};
    return routing::reversed_search(f.tt_, nullptr, search_state, algo_state,
                                    f.first_n_query(k), direction::kForward,
                                    std::nullopt, reversed_order::kDeparture)
        .search_stats_.n_execute_fwd_;
  };
  EXPECT_EQ(1U, n_forward(3U));
  EXPECT_EQ(2U, n_forward(4U));

  // more than the profile holds: everything, in a complete prefix
  auto const all = keys(test::search_reversed(
      f.tt_, nullptr, f.first_n_query(20U), direction::kForward,
      reversed_order::kDeparture));
  EXPECT_EQ(profile, all) << str(all);

  // orientation
  auto const results = test::search_reversed(
      f.tt_, nullptr, f.first_n_query(n), direction::kForward,
      reversed_order::kDeparture);
  auto prev = unixtime_t::min();
  for (auto const& j : results) {
    EXPECT_LE(prev, j.start_time_);
    prev = j.start_time_;
    ASSERT_FALSE(j.legs_.empty());
    EXPECT_EQ(get_special_station(special_station::kStart),
              j.legs_.front().from_);
    EXPECT_EQ(get_special_station(special_station::kEnd), j.legs_.back().to_);
    EXPECT_EQ(j.start_time_, j.legs_.front().dep_time_);
    EXPECT_EQ(j.dest_time_, j.legs_.back().arr_time_);
  }
}

TEST(routing, reversed_search_departure_order_arrive_by) {
  auto f = fixture{true};
  using routing::reversed_order;
  // journeys arriving at or before 16:00 (3 min egress: trains by 15:57):
  // latest arrivals 15:23 (14:10, 1), 14:23 (13:10, 1), 14:03 (11:55, 0)
  auto const by_dep = keys(test::search_reversed(
      f.tt_, nullptr, f.last_n_by_query(3U), direction::kBackward,
      reversed_order::kDeparture));
  EXPECT_EQ((std::set<jkey_t>{{f.day_ + 14h + 10min, f.day_ + 15h + 23min, 1U},
                              {f.day_ + 13h + 10min, f.day_ + 14h + 23min, 1U},
                              {f.day_ + 11h + 55min, f.day_ + 14h + 3min, 0U}}),
            by_dep)
      << str(by_dep);
  // latest departures instead: 14:10, 13:10, 12:10
  auto const by_arr = keys(test::search_reversed(
      f.tt_, nullptr, f.last_n_by_query(3U), direction::kBackward,
      reversed_order::kArrival));
  EXPECT_EQ((std::set<jkey_t>{{f.day_ + 14h + 10min, f.day_ + 15h + 23min, 1U},
                              {f.day_ + 13h + 10min, f.day_ + 14h + 23min, 1U},
                              {f.day_ + 12h + 10min, f.day_ + 13h + 23min, 1U}}),
            by_arr)
      << str(by_arr);
}
