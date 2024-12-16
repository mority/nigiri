#pragma once

#include <cinttypes>

#include "nigiri/types.h"

namespace nigiri::routing {

static constexpr auto const kMaxTransfers = std::uint8_t{15U};
static constexpr auto const kMaxTravelTime = 5_days;
static constexpr auto const kMaxSearchIntervalSize = 2_days;
static constexpr auto const kShortSearchIntervalSize = 1_days;
static constexpr auto const kMaxVias = 2;

}  // namespace nigiri::routing
