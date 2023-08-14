#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct earliest_times {

#ifdef TB_MIN_WALK
  struct earliest_time {
    earliest_time() : time_arr_(0U), time_walk_(0U) {}
    earliest_time(std::uint16_t const time_arr,
                  std::uint16_t const time_walk,
                  bitfield const& bf)
        : time_arr_(time_arr), time_walk_(time_walk), bf_(bf) {}

    std::uint16_t time_arr_;
    std::uint16_t time_walk_;
    bitfield bf_;
  };

  void update_walk(location_idx_t,
                   std::uint16_t time_arr_new,
                   std::uint16_t time_walk_new,
                   bitfield const& bf,
                   bitfield* impr);
#else
  struct earliest_time {
    earliest_time() : time_{0U} {}
    earliest_time(std::uint16_t const time, bitfield const& bf)
        : time_(time), bf_(bf) {}

    std::uint16_t time_;
    bitfield bf_;
  };

  void update(location_idx_t,
              std::uint16_t time_new,
              bitfield const& bf,
              bitfield* impr);
#endif

  void reset() noexcept { times_.clear(); }

  bitfield bf_new_;
  mutable_fws_multimap<location_idx_t, earliest_time> times_;
};

}  // namespace nigiri::routing::tripbased