#pragma once

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::loader {

void build_adjacencies(timetable&, profile_idx_t);

}  // namespace nigiri::loader