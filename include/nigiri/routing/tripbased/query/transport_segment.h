#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/types.h"

#define TRANSFERRED_FROM_NULL std::numeric_limits<std::uint32_t>::max()

namespace nigiri {
struct timetable;
}  // namespace nigiri

namespace nigiri::routing::tripbased {

struct transport_segment_idx_t {
  transport_segment_idx_t(std::uint32_t day_offset, std::uint32_t transport_idx)
      : day_offset_(day_offset), transport_idx_(transport_idx) {}

  transport_segment_idx_t(day_idx_t base,
                          day_idx_t transport_day,
                          transport_idx_t transport_idx)
      : day_offset_(calc_day_offset(base, transport_day)),
        transport_idx_(transport_idx.v_) {}

  explicit transport_segment_idx_t(std::uint32_t value)
      : day_offset_(value >> 29), transport_idx_(value & 536870911U) {}

  transport_segment_idx_t(transport_segment_idx_t const& o)
      : day_offset_(o.day_offset_), transport_idx_(o.transport_idx_) {}

  transport_segment_idx_t& operator=(transport_segment_idx_t o) {
    std::swap(day_offset_, o.day_offset_);
  }

  static std::uint32_t calc_day_offset(day_idx_t const base,
                                       day_idx_t const transport_day) {
    auto const day_offset = static_cast<std::int32_t>(transport_day.v_) -
                            static_cast<std::int32_t>(base.v_) + 5;
    assert(0 <= day_offset && day_offset <= 7);
    return static_cast<std::uint32_t>(day_offset);
  }

  day_idx_t get_transport_day(day_idx_t const base) const {
    return day_idx_t{base.v_ + static_cast<std::int32_t>(day_offset_) - 5};
  }

  transport_idx_t get_transport_idx() const {
    return transport_idx_t{transport_idx_};
  }

  std::uint32_t value() const {
    return static_cast<std::uint32_t>(day_offset_) << 29 |
           static_cast<std::uint32_t>(transport_idx_);
  }

  // store day offset of the instance in upper bits of transport idx
  std::uint32_t day_offset_ : 3;
  std::uint32_t transport_idx_ : 29;
};

struct transport_segment {
#if defined(TB_MIN_WALK) && defined(TB_CACHE_PRESSURE_REDUCTION)
  explicit transport_segment(transport_segment_idx_t transport_segment_idx,
                             stop_idx_t stop_idx_start,
                             stop_idx_t stop_idx_end,
                             std::uint32_t transferred_from,
                             std::uint16_t time_walk)
      : transport_segment_idx_(transport_segment_idx),
        stop_idx_start_(stop_idx_start),
        stop_idx_end_(stop_idx_end),
        transferred_from_(transferred_from),
        time_walk_(time_walk) {}
#elif defined(TB_TRANSFER_CLASS) && defined(TB_CACHE_PRESSURE_REDUCTION)
  explicit transport_segment(transport_segment_idx_t transport_segment_idx,
                             stop_idx_t stop_idx_start,
                             stop_idx_t stop_idx_end,
                             std::uint32_t transferred_from,
                             std::uint8_t transfer_class_max,
                             std::uint8_t transfer_class_sum)
      : transport_segment_idx_(transport_segment_idx),
        stop_idx_start_(stop_idx_start),
        stop_idx_end_(stop_idx_end),
        transferred_from_(transferred_from),
        transfer_class_max_(transfer_class_max),
        transfer_class_sum_(transfer_class_sum) {}
#else
  explicit transport_segment(transport_segment_idx_t transport_segment_idx,
                             stop_idx_t stop_idx_start,
                             stop_idx_t stop_idx_end,
                             std::uint32_t transferred_from)
      : transport_segment_idx_(transport_segment_idx),
        stop_idx_start_(stop_idx_start),
        stop_idx_end_(stop_idx_end),
        transferred_from_(transferred_from) {}
#endif

  day_idx_t get_transport_day(day_idx_t const base) const {
    return transport_segment_idx_.get_transport_day(base);
  }

  transport_idx_t get_transport_idx() const {
    return transport_segment_idx_.get_transport_idx();
  }

  std::uint32_t get_transport_segment_idx() const {
    return transport_segment_idx_.value();
  }

  void print(std::ostream&, timetable const&) const;

  // store day offset of the instance in upper bits of transport idx
  transport_segment_idx_t transport_segment_idx_;

  stop_idx_t stop_idx_start_;
  stop_idx_t stop_idx_end_;

  // queue index of the segment from which we transferred to this segment
  std::uint32_t transferred_from_;

#ifdef TB_CACHE_PRESSURE_REDUCTION
  union {
    unixtime_t time_prune_;
    bool no_prune_;
  };
#ifdef TB_MIN_WALK
  std::uint16_t time_walk_;
#elifdef TB_TRANSFER_CLASS
  std::uint8_t transfer_class_max_;
  std::uint8_t transfer_class_sum_;
#endif
#endif
};

}  // namespace nigiri::routing::tripbased