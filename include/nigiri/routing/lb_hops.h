#pragma once

#include "nigiri/timetable.h"

namespace nigiri::routing {

template <direction SearchDir>
void lb_hops(timetable const&,
             query const&,
             std::vector<location_idx_t>& queue,
             vector_map<location_idx_t, std::uint8_t>& location_hops);

}  // namespace nigiri::routing