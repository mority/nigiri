#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_reduction {

#ifdef TB_MIN_WALK
  struct rr_entry {
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

#ifdef TB_TRANSFER_CLASS
  struct rr_entry {
    std::uint16_t time_;
    std::uint8_t transfer_class_;
    bitfield bf_;
  };

  void update_class(location_idx_t,
                    std::uint16_t time_new,
                    std::uint8_t transfer_class_new,
                    bitfield const& bf,
                    bitfield* impr);
#else
  struct rr_entry {
    std::uint16_t time_;
    bitfield bf_;
  };

  void update(location_idx_t,
              std::uint16_t time_new,
              bitfield const& bf,
              bitfield* impr);
#endif
#endif

  void reset() noexcept { times_.clear(); }

  bitfield bf_new_;
  mutable_fws_multimap<location_idx_t, rr_entry> times_;
};

}  // namespace nigiri::routing::tripbased