#include "./test_data.h"
#include "nigiri/loader/hrd/load_timetable.h"

#include "nigiri/loader/gtfs/files.h"
#include "../../loader/hrd/hrd_timetable.h"

using namespace nigiri;
using namespace nigiri::loader::gtfs;

namespace nigiri::routing::tripbased::test {

constexpr auto const agency_file_content = std::string_view{
    R"(agency_id,agency_name,agency_url,agency_timezone
DTA,Demo Transit Authority,,Europe/London
)"};

constexpr auto const transfers_file_content =
    std::string_view{R"(from_stop_id,to_stop_id,transfer_type,min_transfer_time
)"};

constexpr auto const frequencies_file_content =
    std::string_view{R"(trip_id,start_time,end_time,headway_secs
)"};

constexpr auto const calendar_file_content = std::string_view{
    R"(service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
WE,0,0,0,0,0,1,1,20210301,20210307
WD,1,1,1,1,1,0,0,20210301,20210307
MON,1,0,0,0,0,0,0,20210301,20210307
TUE,0,1,0,0,0,0,0,20210301,20210307
WED,0,0,1,0,0,0,0,20210301,20210307
THU,0,0,0,1,0,0,0,20210301,20210307
FRI,0,0,0,0,1,0,0,20210301,20210307
SAT,0,0,0,0,0,1,0,20210301,20210307
SUN,0,0,0,0,0,0,1,20210301,20210307
)"};

constexpr auto const calendar_dates_file_content =
    R"(service_id,date,exception_type
)";

constexpr auto const simple_stops_file_content = std::string_view{
    R"(stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
)"};

constexpr auto const no_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const no_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,THU,R1_THU,R1_THU,2
)";

constexpr auto const no_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,12:00:00,12:00:00,S1,1,0,0
R1_THU,06:00:00,06:00:00,S1,0,0,0
R1_THU,07:00:00,07:00:00,S2,1,0,0
)";

loader::mem_dir no_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{simple_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{no_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{no_transfer_trips_file_content}},
       {path{kStopTimesFile}, std::string{no_transfer_stop_times_content}}}};
}

constexpr auto const same_day_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const same_day_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,MON,R1_MON,R1_MON,2
)";

constexpr auto const same_day_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,06:00:00,06:00:00,S1,1,0,0
R1_MON,12:00:00,12:00:00,S1,0,0,0
R1_MON,13:00:00,13:00:00,S2,1,0,0
)";

loader::mem_dir same_day_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{simple_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{same_day_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{same_day_transfer_trips_file_content}},
       {path{kStopTimesFile},
        std::string{same_day_transfer_stop_times_content}}}};
}

constexpr auto const long_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const long_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,THU,R1_THU,R1_THU,2
)";

constexpr auto const long_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,76:00:00,76:00:00,S1,1,0,0
R1_THU,06:00:00,06:00:00,S1,0,0,0
R1_THU,07:00:00,07:00:00,S2,1,0,0
)";

loader::mem_dir long_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{simple_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{long_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{long_transfer_trips_file_content}},
       {path{kStopTimesFile}, std::string{long_transfer_stop_times_content}}}};
}

constexpr auto const weekday_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const weekday_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,WD,R0_WD,R0_WD,1
R1,WD,R1_WD,R1_WD,2
)";

constexpr auto const weekday_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_WD,00:00:00,00:00:00,S0,0,0,0
R0_WD,02:00:00,02:00:00,S1,1,0,0
R1_WD,01:00:00,01:00:00,S1,0,0,0
R1_WD,03:00:00,03:00:00,S2,1,0,0
)";

loader::mem_dir weekday_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{simple_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{weekday_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{weekday_transfer_trips_file_content}},
       {path{kStopTimesFile},
        std::string{weekday_transfer_stop_times_content}}}};
}

}  // namespace nigiri::routing::tripbased::test