#pragma once

#include "nigiri/routing/journey.h"
#include "nigiri/routing/query.h"

namespace nigiri::routing {

// True if the journey's first or last offset, or both together if they use
// the same transport mode, take at least as long as the direct connection
// with that mode (query::direct_durations_). Such a journey is not better than
// going direct. Journeys without legs (not reconstructed) are never filtered.
bool is_not_better_than_direct(query const&, journey const&);

}  // namespace nigiri::routing
