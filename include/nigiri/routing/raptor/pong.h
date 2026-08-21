#pragma once

// `get_earliest_alternative` lives in its own translation unit; re-exported
// here so existing includers of pong.h keep working.
#include "nigiri/routing/get_earliest_alternative.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/search.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

static constexpr auto kMinLookAhead = 1_days;

template <typename AlgoState>
routing_result pong_search(
    timetable const&,
    rt_timetable const*,
    search_state&,
    AlgoState&,
    query,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing