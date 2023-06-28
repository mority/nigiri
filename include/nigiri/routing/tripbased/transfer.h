#pragma once

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct transfer {
  transfer(std::uint32_t const& bf_idx,
           std::uint32_t const& transport_idx_to,
           std::uint16_t const& stop_idx_to,
           std::uint16_t const& passes_midnight)
      : bitfield_idx_(bf_idx),
        transport_idx_to_(transport_idx_to),
        stop_idx_to_(stop_idx_to),
        passes_midnight_(passes_midnight) {}

  std::uint64_t value() const {
    return *reinterpret_cast<std::uint64_t const*>(this);
  }

  cista::hash_t hash() const {
    return cista::hash_combine(cista::BASE_HASH, value());
  }

  // the days on which the transfer can take place
  bitfield_idx_t get_bitfield_idx() const {
    return bitfield_idx_t{bitfield_idx_};
  }

  // the transport that is the target of the transfer
  transport_idx_t get_transport_idx_to() const {
    return transport_idx_t{transport_idx_to_};
  }

  // the stop index of the target transport
  stop_idx_t get_stop_idx_to() const {
    return static_cast<std::uint16_t>(stop_idx_to_);
  }

  // bit: 1 -> the transfer passes midnight
  day_idx_t get_passes_midnight() const { return day_idx_t{passes_midnight_}; }

  // the days on which the transfer can take place
  std::uint64_t bitfield_idx_ : BITFIELD_IDX_BITS;

  // the transport that is the target of the transfer
  std::uint64_t transport_idx_to_ : TRANSPORT_IDX_BITS;

  // the stop index of the target transport
  std::uint64_t stop_idx_to_ : STOP_IDX_BITS;

  // bit: 1 -> the transfer passes midnight
  std::uint64_t passes_midnight_ : 1;
};

template <std::size_t NMaxTypes>
constexpr auto static_type_hash(transfer const*,
                                cista::hash_data<NMaxTypes> h) noexcept {
  return h.combine(cista::hash("nigiri::routing::tripbased::transfer"));
}

template <typename Ctx>
inline void serialize(Ctx&, transfer const*, cista::offset_t const) {}

template <typename Ctx>
inline void deserialize(Ctx const&, transfer*) {}

}  // namespace nigiri::routing::tripbased