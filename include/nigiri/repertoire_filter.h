#pragma once

#include <vector>

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri {

void repertoire_filter(std::vector<nigiri::location_idx_t>& sorted_in,
                       std::vector<nigiri::location_idx_t>& out,
                       nigiri::timetable const&,
                       std::uint8_t stations_per_route);

}  // namespace nigiri