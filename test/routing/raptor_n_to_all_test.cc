#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/special_stations.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using nigiri::test::raptor_n_to_all_search;

void print(std::ostream& out,
           timetable const& tt,
           std::vector<std::vector<unixtime_t>> const& times) {
  out << "\n";
  for (auto l = special_stations_names.size();
       l != tt.n_locations() && l != times.size(); ++l) {
    out << tt.locations_.get(location_idx_t{l}).name_ << ":";
    for (auto i = 0U; i != times[l].size(); ++i) {
      if (times[l][i] != unixtime_t{duration_t{0}}) {
        out << " (#rides: " << i << ", " << times[l][i] << ")";
      }
    }
    out << "\n";
  }
  out << "\n";
}

//                                                                11:00
//          A0               B0                            C0--------------+
//    08:00 |                | 09:00                       | 10:00         |
//          |                |                             |               |
//    09:00 V  10:00  11:00  V 10:00     12:00  13:00      V 11:00         |
//          A1-------------->B1---------------+----------->C1              |
//    10:00 |                | 12:00          |  12:30     | 14:00         |
//          |                |                |            |               |
//    11:00 V                V 12:10          |            V 15:00         |
//          A2               B2---------------+            C2<-------------+
//                              12:20                             12:00
mem_dir n_to_all_files() {
  return mem_dir::read(R"__(
"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
MTA,MOTIS Transit Authority,https://motis-project.de/,Europe/London

# calendar_dates.txt
service_id,date,exception_type
D,20241120,1

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A0,A0,,,,,,,
A1,A1,,,,,,,
A2,A2,,,,,,,
B0,B0,,,,,,,
B1,B1,,,,,,,
B2,B2,,,,,,,
C0,C0,,,,,,,
C1,C1,,,,,,,
C2,C2,,,,,,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
A,MTA,A,A,A0 -> A1 -> A2,0
B,MTA,B,B,B0 -> B1 -> B2,0
C,MTA,C,C,C0 -> C1 -> C2,0
ABC,MTA,ABC,ABC,A1 -> B1 -> C1,0
BC,MTA,BC,BC,B2 -> C1,0
CC,MTA,CC,CC,C0 -> C2,0

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
A,D,A_TRIP,A_TRIP,1
B,D,B_TRIP,B_TRIP,2
C,D,C_TRIP,C_TRIP,3
ABC,D,ABC_TRIP,ABC_TRIP,4
BC,D,BC_TRIP,BC_TRIP,5
CC,D,CC_TRIP,CC_TRIP,6

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
A_TRIP,08:00,08:00,A0,0,0,0
A_TRIP,09:00,10:00,A1,1,0,0
A_TRIP,11:00,11:00,A2,2,0,0
B_TRIP,09:00,09:00,B0,0,0,0
B_TRIP,10:00,12:00,B1,1,0,0
B_TRIP,12:10,12:10,B2,2,0,0
C_TRIP,10:00,10:00,C0,0,0,0
C_TRIP,11:00,14:00,C1,1,0,0
C_TRIP,15:00,15:00,C2,2,0,0
ABC_TRIP,10:00,10:00,A1,0,0,0
ABC_TRIP,11:00,12:00,B1,1,0,0
ABC_TRIP,13:00,13:00,C1,2,0,0
BC_TRIP,12:20,12:20,B2,0,0,0
BC_TRIP,12:30,12:30,C1,0,0,0
CC_TRIP,11:00,11:00,C0,0,0,0
CC_TRIP,12:00,12:00,C2,0,0,0

)__");
}

constexpr interval<std::chrono::sys_days> n_to_all_period() {
  using namespace date;
  constexpr auto const from = (2024_y / November / 19).operator sys_days();
  constexpr auto const to = (2024_y / November / 21).operator sys_days();
  return {from, to};
}

constexpr auto const one_to_all_fwd_times = R"(
A0: (#rides: 0, 2024-11-20 08:00)
A1: (#rides: 1, 2024-11-20 09:02)
A2: (#rides: 1, 2024-11-20 11:02)
B0:
B1: (#rides: 2, 2024-11-20 11:02)
B2: (#rides: 3, 2024-11-20 12:12)
C0:
C1: (#rides: 2, 2024-11-20 13:02) (#rides: 4, 2024-11-20 12:32)
C2: (#rides: 3, 2024-11-20 15:02)

)";

TEST(routing, one_to_all_fwd) {
  timetable tt;
  tt.date_range_ = n_to_all_period();
  register_special_stations(tt);
  gtfs::load_timetable(loader_config{}, source_idx_t{0U}, n_to_all_files(), tt);
  finalize(tt);

  auto const times = raptor_n_to_all_search(
      tt, nullptr, {"A0"},
      interval{unixtime_t{sys_days{2024_y / November / 20}},
               unixtime_t{sys_days{2024_y / November / 21}}},
      direction::kForward);

  std::stringstream ss;
  print(ss, tt, times);
  EXPECT_EQ(std::string_view{one_to_all_fwd_times}, ss.str());
}

constexpr auto const one_to_all_bwd_times = R"(
A0: (#rides: 3, 2024-11-20 07:58)
A1: (#rides: 2, 2024-11-20 09:58)
A2:
B0: (#rides: 3, 2024-11-20 08:58)
B1: (#rides: 2, 2024-11-20 11:58)
B2: (#rides: 2, 2024-11-20 12:18)
C0: (#rides: 1, 2024-11-20 10:58)
C1: (#rides: 1, 2024-11-20 13:58)
C2: (#rides: 0, 2024-11-20 15:00)

)";

TEST(routing, one_to_all_bwd) {
  timetable tt;
  tt.date_range_ = n_to_all_period();
  register_special_stations(tt);
  gtfs::load_timetable(loader_config{}, source_idx_t{0U}, n_to_all_files(), tt);
  finalize(tt);

  std::vector<std::string> const from{"C2"};
  auto const times = raptor_n_to_all_search(
      tt, nullptr, from,
      interval{unixtime_t{sys_days{2024_y / November / 20}},
               unixtime_t{sys_days{2024_y / November / 21}}},
      direction::kBackward);

  std::stringstream ss;
  print(ss, tt, times);
  EXPECT_EQ(std::string_view{one_to_all_bwd_times}, ss.str());
}

constexpr auto const two_to_all_fwd_times = R"(
A0: (#rides: 0, 2024-11-20 08:00)
A1: (#rides: 1, 2024-11-20 09:02)
A2: (#rides: 1, 2024-11-20 11:02)
B0: (#rides: 0, 2024-11-20 09:00)
B1: (#rides: 1, 2024-11-20 10:02)
B2: (#rides: 1, 2024-11-20 12:12)
C0:
C1: (#rides: 2, 2024-11-20 12:32)
C2: (#rides: 3, 2024-11-20 15:02)

)";

TEST(routing, two_to_all_fwd) {
  timetable tt;
  tt.date_range_ = n_to_all_period();
  register_special_stations(tt);
  gtfs::load_timetable(loader_config{}, source_idx_t{0U}, n_to_all_files(), tt);
  finalize(tt);

  auto const times = raptor_n_to_all_search(
      tt, nullptr, {"A0", "B0"},
      interval{unixtime_t{sys_days{2024_y / November / 20}},
               unixtime_t{sys_days{2024_y / November / 21}}},
      direction::kForward);

  std::stringstream ss;
  print(ss, tt, times);
  EXPECT_EQ(std::string_view{two_to_all_fwd_times}, ss.str());
}

constexpr auto const two_to_all_bwd_times = R"(
A0: (#rides: 1, 2024-11-20 07:58)
A1: (#rides: 1, 2024-11-20 09:58)
A2: (#rides: 0, 2024-11-20 11:00)
B0: (#rides: 3, 2024-11-20 08:58)
B1: (#rides: 2, 2024-11-20 11:58)
B2: (#rides: 2, 2024-11-20 12:18)
C0: (#rides: 1, 2024-11-20 10:58)
C1: (#rides: 1, 2024-11-20 13:58)
C2: (#rides: 0, 2024-11-20 15:00)

)";

TEST(routing, two_to_all_bwd) {
  timetable tt;
  tt.date_range_ = n_to_all_period();
  register_special_stations(tt);
  gtfs::load_timetable(loader_config{}, source_idx_t{0U}, n_to_all_files(), tt);
  finalize(tt);

  auto const times = raptor_n_to_all_search(
      tt, nullptr, {"A2", "C2"},
      interval{unixtime_t{sys_days{2024_y / November / 20}},
               unixtime_t{sys_days{2024_y / November / 21}}},
      direction::kBackward);

  std::stringstream ss;
  print(ss, tt, times);
  EXPECT_EQ(std::string_view{two_to_all_bwd_times}, ss.str());
}