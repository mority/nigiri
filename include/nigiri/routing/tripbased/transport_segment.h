#pragma once

#include "nigiri/routing/tripbased/bits.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing::tripbased {

using transport_segment_idx_t = std::uint32_t;

static constexpr transport_segment_idx_t day_offset_mask{
    0b1110'0000'0000'0000'0000'0000'0000'0000};

static constexpr transport_segment_idx_t transport_idx_mask{
    0b0001'1111'1111'1111'1111'1111'1111'1111};

static inline transport_segment_idx_t embed_day_offset(
    const day_idx_t base,
    const day_idx_t transport_day,
    const transport_idx_t transport_idx) {
  return (((static_cast<transport_segment_idx_t>(transport_day.v_) +
            QUERY_DAY_SHIFT) -
           static_cast<transport_segment_idx_t>(base.v_))
          << (32U - DAY_OFFSET_BITS)) |
         transport_idx.v_;
}

static inline std::uint32_t day_offset(
    const transport_segment_idx_t transport_segment_idx) {
  return (transport_segment_idx & day_offset_mask) >> (32U - DAY_OFFSET_BITS);
}

static inline day_idx_t transport_day(
    const day_idx_t base, const transport_segment_idx_t transport_segment_idx) {
  return day_idx_t{base.v_ +
                   static_cast<int>(day_offset(transport_segment_idx)) -
                   QUERY_DAY_SHIFT};
}

static inline transport_idx_t transport_idx(
    const transport_segment_idx_t transport_segment_idx) {
  return transport_idx_t{transport_segment_idx & transport_idx_mask};
}

struct transport_segment {
  transport_segment(transport_segment_idx_t transport_segment_idx,
                    std::uint32_t stop_idx_start,
                    std::uint32_t stop_idx_end,
                    std::uint32_t transferred_from)
      : transport_segment_idx_(transport_segment_idx),
        stop_idx_start_(stop_idx_start),
        stop_idx_end_(stop_idx_end),
        transferred_from_(transferred_from) {}

  day_idx_t get_transport_day(day_idx_t const base) const {
    return transport_day(base, transport_segment_idx_);
  }

  transport_idx_t get_transport_idx() const {
    return transport_idx(transport_segment_idx_);
  }

  void print(std::ostream&, timetable const&) const;

  // store day offset of the instance in upper bits of transport idx
  transport_segment_idx_t transport_segment_idx_;

  std::uint32_t stop_idx_start_ : STOP_IDX_BITS;
  std::uint32_t stop_idx_end_ : STOP_IDX_BITS;

  // queue index of the segment from which we transferred to this segment
  std::uint32_t transferred_from_;
};

}  // namespace nigiri::routing::tripbased