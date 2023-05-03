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
DLY,1,1,1,1,1,1,1,20210301,20210307
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

constexpr auto const daily_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const daily_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,DLY,R0_DLY,R0_DLY,1
R1,DLY,R1_DLY,R1_DLY,2
)";

constexpr auto const daily_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_DLY,00:00:00,00:00:00,S0,0,0,0
R0_DLY,02:00:00,02:00:00,S1,1,0,0
R1_DLY,03:00:00,03:00:00,S1,0,0,0
R1_DLY,04:00:00,04:00:00,S2,1,0,0
)";

loader::mem_dir daily_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{simple_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{daily_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{daily_transfer_trips_file_content}},
       {path{kStopTimesFile}, std::string{daily_transfer_stop_times_content}}}};
}

constexpr auto const five_stops_file_content = std::string_view{
    R"(stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,
)"};

constexpr auto const earlier_stop_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S4",2
)"};

constexpr auto const earlier_stop_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON0,R0_MON0,0
R0,MON,R0_MON1,R0_MON1,0
)";

constexpr auto const earlier_stop_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON0,00:00:00,00:00:00,S0,0,0,0
R0_MON0,01:00:00,01:00:00,S1,1,0,0
R0_MON0,02:00:00,02:00:00,S2,2,0,0
R0_MON0,03:00:00,03:00:00,S3,3,0,0
R0_MON0,04:00:00,04:00:00,S1,4,0,0
R0_MON0,05:00:00,05:00:00,S4,5,0,0
R0_MON1,04:00:00,04:00:00,S0,0,0,0
R0_MON1,05:00:00,05:00:00,S1,1,0,0
R0_MON1,06:00:00,06:00:00,S2,2,0,0
R0_MON1,07:00:00,07:00:00,S3,3,0,0
R0_MON1,08:00:00,08:00:00,S1,4,0,0
R0_MON1,09:00:00,09:00:00,S4,5,0,0
)";

loader::mem_dir earlier_stop_transfer_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{five_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile},
            std::string{earlier_stop_transfer_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile},
            std::string{earlier_stop_transfer_trips_file_content}},
           {path{kStopTimesFile},
            std::string{earlier_stop_transfer_stop_times_content}}}};
}

constexpr auto const earlier_transport_transfer_routes_file_content =
    std::string_view{
        R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S4",2
)"};

constexpr auto const earlier_transport_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON0,R0_MON0,0
R0,MON,R0_MON1,R0_MON1,0
)";

constexpr auto const earlier_transport_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON0,00:00:00,00:00:00,S0,0,0,0
R0_MON0,01:00:00,01:00:00,S1,1,0,0
R0_MON0,02:00:00,02:00:00,S2,2,0,0
R0_MON0,03:00:00,03:00:00,S3,3,0,0
R0_MON0,04:00:00,04:00:00,S1,4,0,0
R0_MON0,05:00:00,05:00:00,S4,5,0,0
R0_MON1,02:00:00,02:00:00,S0,0,0,0
R0_MON1,03:00:00,03:00:00,S1,1,0,0
R0_MON1,04:00:00,04:00:00,S2,2,0,0
R0_MON1,05:00:00,05:00:00,S3,3,0,0
R0_MON1,05:00:00,05:00:00,S1,4,0,0
R0_MON1,06:00:00,06:00:00,S4,5,0,0
)";

loader::mem_dir earlier_transport_transfer_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{five_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile},
            std::string{earlier_transport_transfer_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile},
            std::string{earlier_transport_transfer_trips_file_content}},
           {path{kStopTimesFile},
            std::string{earlier_transport_transfer_stop_times_content}}}};
}

constexpr auto const uturn_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1 -> S2",2
R1,DTA,R1,R1,"S2 -> S1 -> S3",2
)"};

constexpr auto const uturn_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,0
)";

constexpr auto const uturn_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S1,1,0,0
R0_MON,02:00:00,02:00:00,S2,2,0,0
R1_MON,03:00:00,03:00:00,S2,0,0,0
R1_MON,04:00:00,04:00:00,S1,1,0,0
R1_MON,05:00:00,05:00:00,S3,2,0,0
)";

loader::mem_dir uturn_transfer_files() {
  using std::filesystem::path;
  return {
      {{path{kAgencyFile}, std::string{agency_file_content}},
       {path{kStopFile}, std::string{five_stops_file_content}},
       {path{kCalenderFile}, std::string{calendar_file_content}},
       {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
       {path{kTransfersFile}, std::string{transfers_file_content}},
       {path{kRoutesFile}, std::string{uturn_transfer_routes_file_content}},
       {path{kFrequenciesFile}, std::string{frequencies_file_content}},
       {path{kTripsFile}, std::string{uturn_transfer_trips_file_content}},
       {path{kStopTimesFile}, std::string{uturn_transfer_stop_times_content}}}};
}

constexpr auto const unnecessary0_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1 -> S2",2
R1,DTA,R1,R1,"S1 -> S2",2
)"};

constexpr auto const unnecessary0_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,0
)";

constexpr auto const unnecessary0_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S1,1,0,0
R0_MON,02:00:00,02:00:00,S2,2,0,0
R1_MON,01:00:00,01:10:00,S1,0,0,0
R1_MON,04:00:00,04:00:00,S2,1,0,0
)";

loader::mem_dir unnecessary0_transfer_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{simple_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile},
            std::string{unnecessary0_transfer_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile},
            std::string{unnecessary0_transfer_trips_file_content}},
           {path{kStopTimesFile},
            std::string{unnecessary0_transfer_stop_times_content}}}};
}

constexpr auto const six_stops_file_content = std::string_view{
    R"(stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,,,,,,
S2,S2,,,,,,
S3,S3,,,,,,
S4,S4,,,,,,
S5,S5,,,,,,
)"};

constexpr auto const unnecessary1_transfer_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> -> S2 -> S3 -> S4",2
R1,DTA,R1,R1,"S1 -> S2 -> S3 -> S5",2
)"};

constexpr auto const unnecessary1_transfer_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,0
R1,MON,R1_MON,R1_MON,0
)";

constexpr auto const unnecessary1_transfer_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,01:00:00,01:00:00,S2,1,0,0
R0_MON,02:00:00,02:00:00,S3,2,0,0
R0_MON,03:00:00,03:00:00,S4,3,0,0
R1_MON,00:10:00,00:10:00,S1,0,0,0
R1_MON,01:10:00,01:10:00,S2,1,0,0
R1_MON,02:10:00,02:10:00,S3,2,0,0
R1_MON,03:10:00,03:10:00,S5,3,0,0
)";

loader::mem_dir unnecessary1_transfer_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{six_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile},
            std::string{unnecessary1_transfer_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile},
            std::string{unnecessary1_transfer_trips_file_content}},
           {path{kStopTimesFile},
            std::string{unnecessary1_transfer_stop_times_content}}}};
}

constexpr auto const enqueue_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,R0,2
)"};

constexpr auto const enqueue_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,DLY,R0_0,R0_0,0
R0,DLY,R0_1,R0_1,0
)";

constexpr auto const enqueue_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_0,00:00:00,00:00:00,S0,0,0,0
R0_0,01:00:00,01:00:00,S1,1,0,0
R0_0,02:00:00,02:00:00,S2,2,0,0
R0_0,03:00:00,03:00:00,S3,3,0,0
R0_0,04:00:00,04:00:00,S4,4,0,0
R0_0,05:00:00,05:00:00,S5,5,0,0
R0_1,01:00:00,01:00:00,S0,0,0,0
R0_1,02:00:00,02:00:00,S1,1,0,0
R0_1,03:00:00,03:00:00,S2,2,0,0
R0_1,04:00:00,04:00:00,S3,3,0,0
R0_1,05:00:00,05:00:00,S4,4,0,0
R0_1,06:00:00,06:00:00,S5,5,0,0
)";

loader::mem_dir enqueue_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{six_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile}, std::string{enqueue_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile}, std::string{enqueue_trips_file_content}},
           {path{kStopTimesFile}, std::string{enqueue_stop_times_content}}}};
}

constexpr auto const footpath_stops_file_content = std::string_view{
    R"(stop_id,stop_name,stop_desc,stop_lat,stop_lon,stop_url,location_type,parent_station
S0,S0,,,,,,
S1,S1,footpath_start,49.87296,8.65152,,,
S2,S2,footpath_end,49.87269, 8.65078,,,
S3,S3,,,,,,
)"};

constexpr auto const footpath_routes_file_content = std::string_view{
    R"(route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
R0,DTA,R0,R0,"S0 -> S1",2
R1,DTA,R1,R1,"S2 -> S3",2
)"};

constexpr auto const footpath_trips_file_content =
    R"(route_id,service_id,trip_id,trip_headsign,block_id
R0,MON,R0_MON,R0_MON,1
R1,MON,R1_MON,R1_MON,2
)";

constexpr auto const footpath_stop_times_content =
    R"(trip_id,arrival_time,departure_time,stop_id,stop_sequence,pickup_type,drop_off_type
R0_MON,00:00:00,00:00:00,S0,0,0,0
R0_MON,06:00:00,06:00:00,S1,1,0,0
R1_MON,12:00:00,12:00:00,S2,0,0,0
R1_MON,13:00:00,13:00:00,S3,1,0,0
)";

loader::mem_dir footpath_files() {
  using std::filesystem::path;
  return {{{path{kAgencyFile}, std::string{agency_file_content}},
           {path{kStopFile}, std::string{footpath_stops_file_content}},
           {path{kCalenderFile}, std::string{calendar_file_content}},
           {path{kCalendarDatesFile}, std::string{calendar_dates_file_content}},
           {path{kTransfersFile}, std::string{transfers_file_content}},
           {path{kRoutesFile}, std::string{footpath_routes_file_content}},
           {path{kFrequenciesFile}, std::string{frequencies_file_content}},
           {path{kTripsFile}, std::string{footpath_trips_file_content}},
           {path{kStopTimesFile}, std::string{footpath_stop_times_content}}}};
}

}  // namespace nigiri::routing::tripbased::test