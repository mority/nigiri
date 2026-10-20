#pragma once

#include <cassert>
#include <iosfwd>

#include "nigiri/types.h"

#include "utl/verify.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tb {

using segment_idx_t = cista::strong<std::uint32_t, struct _segment_idx>;

using tb_bitfield_idx_t = cista::strong<std::uint32_t, struct _tb_bitfield_idx>;

struct transfer {
  template <typename Ctx>
  friend void serialize(Ctx&, transfer const*, cista::offset_t) {}

  template <typename Ctx>
  friend void deserialize(Ctx const&, transfer*) {}

  template <std::size_t NMaxTypes>
  constexpr friend auto static_type_hash(transfer const*,
                                         cista::hash_data<NMaxTypes> h) {
    return h.combine(cista::hash("tb_data::transfer::v2"));
  }

  segment_idx_t to_segment_;
  tb_bitfield_idx_t traffic_days_;

  // route, transport offset in the route, stop index in the route and day
  // offset of the target, packed as in tb_data::transfer_layout_
  std::uint64_t packed_;
};

static_assert(sizeof(transfer) == 16);

// The bit widths of the packed fields of a transfer, chosen for the timetable
// (see get_transfer_layout). From the lowest bits: day offset, stop index,
// transport offset, route.
struct transfer_layout {
  static constexpr auto const kDayOffsetBits = 4U;

  std::uint64_t pack(route_idx_t const r,
                     std::uint32_t const transport_offset,
                     stop_idx_t const to_segment_offset,
                     std::int8_t const day_offset) const {
    utl::verify(day_offset >= -8 && day_offset < 8, "invalid day offset  {}",
                day_offset);
    assert(to_idx(r) <= mask(route_bits_));
    assert(transport_offset <= mask(transport_bits_));
    assert(to_segment_offset <= mask(stop_bits_));
    return (std::uint64_t{to_idx(r)} << route_shift()) |
           (std::uint64_t{transport_offset} << transport_shift()) |
           (std::uint64_t{to_segment_offset} << kDayOffsetBits) |
           static_cast<std::uint64_t>(day_offset + 8);
  }

  route_idx_t route(transfer const& t) const {
    return route_idx_t{
        static_cast<std::uint32_t>(get(t, route_shift(), route_bits_))};
  }

  std::uint32_t transport_offset(transfer const& t) const {
    return static_cast<std::uint32_t>(
        get(t, transport_shift(), transport_bits_));
  }

  stop_idx_t to_segment_offset(transfer const& t) const {
    return static_cast<stop_idx_t>(get(t, kDayOffsetBits, stop_bits_));
  }

  // Shift amount between trip t and trip u:
  // This includes:
  //   - (positive) how many times did t cross midnight until the transfer
  //   - (negative) how many times did u cross midnight until the transfer
  //   - (+1) does the transfer itself cross midnight
  std::int8_t day_offset(transfer const& t) const {
    return static_cast<std::int8_t>(
        static_cast<int>(get(t, 0U, kDayOffsetBits)) - 8);
  }

  static std::uint64_t mask(unsigned const bits) {
    return (std::uint64_t{1U} << bits) - 1U;
  }

  static std::uint64_t get(transfer const& t,
                           unsigned const shift,
                           unsigned const bits) {
    return (t.packed_ >> shift) & mask(bits);
  }

  unsigned transport_shift() const { return kDayOffsetBits + stop_bits_; }
  unsigned route_shift() const { return transport_shift() + transport_bits_; }

  std::uint8_t route_bits_;
  std::uint8_t transport_bits_;
  std::uint8_t stop_bits_;
};

// As many bits as the timetable's routes, the transports of its largest route
// and the stops of its longest route need.
transfer_layout get_transfer_layout(timetable const&);

struct tb_data {
  interval<segment_idx_t> get_segment_range(transport_idx_t const t) const {
    return {transport_first_segment_[t], transport_first_segment_[t + 1]};
  }

  void print(std::ostream&, timetable const&) const;

  profile_idx_t prf_idx_;
  transfer_layout transfer_layout_;
  vector_map<transport_idx_t, segment_idx_t> transport_first_segment_;
  vecvec<segment_idx_t, transfer> segment_transfers_;
  vector_map<segment_idx_t, transport_idx_t> segment_transports_;
  vector_map<tb_bitfield_idx_t, bitfield> bitfields_;
};

}  // namespace nigiri::routing::tb