#pragma once

#include <vector>

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri {

void repertoire_filter(std::vector<nigiri::location_idx_t>& in,
                       std::vector<nigiri::location_idx_t>& out,
                       geo::latlng const&,
                       nigiri::timetable const&,
                       std::uint32_t filter_threshold,
                       std::uint8_t stations_per_route);

}  // namespace nigiri