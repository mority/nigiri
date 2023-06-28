#pragma once

#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

// a transfer before its bitfield is deduplicated
struct expanded_transfer {
  expanded_transfer(bitfield const& bf,
                    transport_idx_t const& transport_idx_to,
                    stop_idx_t const& stop_idx_to,
                    std::uint16_t const& passes_midnight)
      : bf_(bf),
        transport_idx_to_(transport_idx_to),
        stop_idx_to_(stop_idx_to),
        passes_midnight_(passes_midnight) {}

  bitfield bf_;
  transport_idx_t transport_idx_to_;
  stop_idx_t stop_idx_to_;
  std::uint16_t passes_midnight_;
};

template <std::size_t NMaxTypes>
constexpr auto static_type_hash(expanded_transfer const*,
                                cista::hash_data<NMaxTypes> h) noexcept {
  return h.combine(
      cista::hash("nigiri::routing::tripbased::expanded_transfer"));
}

template <typename Ctx>
inline void serialize(Ctx&, expanded_transfer const*, cista::offset_t const) {}

template <typename Ctx>
inline void deserialize(Ctx const&, expanded_transfer*) {}

}  // namespace nigiri::routing::tripbased