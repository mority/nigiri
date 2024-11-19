#include "gtest/gtest.h"

#include "nigiri/loader/gtfs/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "../raptor_search.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;

mem_dir n_to_all_files() {
  return mem_dir::read(R"__(
"(
# agency.txt
agency_id,agency_name,agency_url,agency_timezone
MTA,MOTIS Transit Authority,https://motis-project.de/,Europe/Berlin

# calendar_dates.txt
service_id,date,exception_type
D,20240608,1

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
A,MTA,A,A,A0 -> A2,0
B,MTA,B,B,B0 -> B2,0
C,MTA,C,C,C0 -> C2,0
ABC,MTA,ABC,ABC,ABC,0

# trips.txt
route_id,service_id,trip_id,trip_headsign,block_id
A,D,A_TRIP,A_TRIP,1
B,D,B_TRIP,B_TRIP,2
C,D,C_TRIP,C_TRIP,3
ABC,D,ABC_TRIP,ABC_TRIP,4

# stop_times.txt
trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
A_TRIP,08:00,08:00,A0,0,0,0
A_TRIP,09:00,10:00,A1,1,0,0
A_TRIP,11:00,11:00,A2,2,0,0
B_TRIP,09:00,09:00,B0,0,0,0
B_TRIP,10:00,12:00,B1,1,0,0
B_TRIP,13:00,13:00,B2,2,0,0
C_TRIP,10:00,10:00,C0,0,0,0
C_TRIP,11:00,14:00,C1,1,0,0
C_TRIP,15:00,15:00,C2,2,0,0
ABC_TRIP,10:00,10:00,A1,0,0,0
ABC_TRIP,11:00,12:00,B1,1,0,0
ABC_TRIP,13:00,13:20,C1,2,0,0

)__");
}

constexpr interval<std::chrono::sys_days> n_to_all_period() {
  using namespace date;
  constexpr auto const from = (2024_y / June / 7).operator sys_days();
  constexpr auto const to = (2024_y / June / 9).operator sys_days();
  return {from, to};
}

TEST(routing, one_to_all_forward) {
  constexpr auto const src = source_idx_t{0U};
  auto const config = loader_config{};

  timetable tt;
  tt.date_range_ = n_to_all_period();
  register_special_stations(tt);
  gtfs::load_timetable(config, src, n_to_all_files(), tt);
  finalize(tt);

  auto const results =
      raptor_search(tt, nullptr, "A0",
                    interval{unixtime_t{sys_days{2024_y / June / 8}},
                             unixtime_t{sys_days{2024_y / June / 9}}});

  std::stringstream ss;
  ss << "\n";
  for (auto const& x : results) {
    x.print(ss, tt);
    ss << "\n\n";
  }
  EXPECT_EQ(std::string_view{fwd_journeys}, ss.str());
}

TEST(routing, one_to_all_backward) {}
