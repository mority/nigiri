#include "gtest/gtest.h"

#include <sstream>

#include "nigiri/timetable.h"

#include "../util.h"
#include "absl/strings/internal/str_format/constexpr_parser.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;

mem_dir adjacency_test_tt() {
  return mem_dir::read(R"__(
"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
MTA,MOTIS Transit Authority,https://motis-project.de/,Europe/Berlin

# calendar.txt
service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
SID,1,1,1,1,1,1,1,20260101,20261231

# stops.txt
stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
A,A,,,,,,
B,B,,,,,,
C,C,,,,,,
X,X,,,,,,
Y,Y,,,,,,

# routes.txt
route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R1,MTA,R1,R1,A -> B -> C,0
R2,MTA,R2,R2,A -> B -> C,0
XY,MTA,XY,XY,X -> Y,0

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
R1,SID,R1,R1,0
R2,SID,R2,R2,0
XY,SID,XY,XY,0

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R1,08:00,08:00,A,0,0,0
R1,08:15,08:45,B,1,0,0
R1,09:30,10:30,C,2,0,0
R2,08:00,08:05,A,0,0,0
R2,08:15,09:45,B,1,0,0
R2,09:50,10:30,C,2,0,0
XY,01:00,01:00,X,0,0,0
XY,02:00,02:00,Y,1,0,0
)__");
}

constexpr auto kGtfsDateRange = interval{sys_days{2026_y / February / 27},
                                         sys_days{2026_y / February / 28}};

TEST(loader, build_lb_routes) {
  auto const tt = load_gtfs(adjacency_test_tt, kGtfsDateRange);

  for (auto const& id : std::array<string, 3>{"A", "B", "C"}) {
    auto const l = tt.find(location_id{id, source_idx_t{}}).value();
    ASSERT_EQ(tt.location_lb_routes_[kDefaultProfile][l].size(), 1U);
    auto const r = tt.location_lb_routes_[kDefaultProfile][l][0];
    ASSERT_EQ(tt.lb_route_root_seq_[kDefaultProfile][r].size(), 3U);
    EXPECT_EQ(tt.get_default_name(tt.lb_route_root_seq_[kDefaultProfile][r][0]),
              "A");
    EXPECT_EQ(tt.get_default_name(tt.lb_route_root_seq_[kDefaultProfile][r][1]),
              "B");
    EXPECT_EQ(tt.get_default_name(tt.lb_route_root_seq_[kDefaultProfile][r][2]),
              "C");
    ASSERT_EQ(tt.lb_route_times_[kDefaultProfile][r].size(), 3U);
    EXPECT_EQ(tt.lb_route_times_[kDefaultProfile][r][0], duration_t{10});
    EXPECT_EQ(tt.lb_route_times_[kDefaultProfile][r][1], duration_t{30});
    EXPECT_EQ(tt.lb_route_times_[kDefaultProfile][r][2], duration_t{5});
  }

  for (auto const& id : std::array<string, 2>{"X", "Y"}) {
    auto const l = tt.find(location_id{id, source_idx_t{}}).value();
    ASSERT_EQ(tt.location_lb_routes_[kDefaultProfile][l].size(), 1U);
    auto const r = tt.location_lb_routes_[kDefaultProfile][l][0];
    ASSERT_EQ(tt.lb_route_root_seq_[kDefaultProfile][r].size(), 2U);
    EXPECT_EQ(tt.get_default_name(tt.lb_route_root_seq_[kDefaultProfile][r][0]),
              "X");
    EXPECT_EQ(tt.get_default_name(tt.lb_route_root_seq_[kDefaultProfile][r][1]),
              "Y");
    ASSERT_EQ(tt.lb_route_times_[kDefaultProfile][r].size(), 1U);
    EXPECT_EQ(tt.lb_route_times_[kDefaultProfile][r][0], duration_t{60});
  }
}