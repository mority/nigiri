#pragma once

#include "nigiri/routing/tripbased/settings.h"

#ifdef TB_PREPRO_LB_PRUNING

#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct reached_line_based {

#ifdef TB_MIN_WALK
  struct rlb_entry {
    std::uint32_t otid_;
    std::uint16_t walk_time_;
    bitfield bf_;
  };

  void update_walk(stop_idx_t j,
                   std::uint32_t otid_new,
                   std::uint16_t walk_time_new,
                   bitfield& bf_new);

  void reset_walk(std::size_t num_stops) noexcept;
#elifdef TB_TRANSFER_CLASS
  struct rlb_entry {
    std::uint32_t otid_;
    std::uint8_t transfer_class_;
    bitfield bf_;
  };

  void update_class(stop_idx_t j,
                    std::uint32_t otid_new,
                    std::uint8_t transfer_class_new,
                    bitfield& bf_new);

  void reset_class(std::size_t num_stops) noexcept;
#else
  struct rlb_entry {
    std::int8_t shift_amount_;
    std::uint16_t start_time_;
    bitfield bf_;
  };

  void update(stop_idx_t j,
              std::int8_t shift_amount_new,
              std::uint16_t start_time_new,
              bitfield& bf_new);

  void reset(std::size_t num_stops) noexcept;
#endif
  mutable_fws_multimap<std::uint32_t, rlb_entry> transports_;
};

}  // namespace nigiri::routing::tripbased

#endif