#pragma once

namespace nigiri::routing::tripbased {

static inline std::uint32_t compute_otid(std::int8_t shift,
                                         std::uint16_t time) {
  std::int16_t shift_wide = shift + 128;
  return (static_cast<std::uint32_t>(shift_wide) << 16) + time;
}

static inline std::uint32_t max_otid() {
  return std::numeric_limits<std::uint32_t>::max();
}

}  // namespace nigiri::routing::tripbased