#pragma once

#include <cstdint>

namespace nigiri::routing {

enum class arrivals_mode : std::uint8_t { kOverwrite, kReuse };

}  // namespace nigiri::routing
