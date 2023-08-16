#pragma once

#include "nigiri/routing/tripbased/settings.h"

#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)

#include "nigiri/routing/journey.h"

namespace nigiri::routing::tripbased {

#ifdef TB_MIN_WALK

struct journey_min_walk : journey {

  bool dominates(journey_min_walk const& o) const {
    if (start_time_ <= dest_time_) {
      return transfers_ <= o.transfers_ && start_time_ >= o.start_time_ &&
             dest_time_ <= o.dest_time_ && time_walk_ <= o.time_walk_;
    } else {
      return transfers_ <= o.transfers_ && start_time_ <= o.start_time_ &&
             dest_time_ >= o.dest_time_ && time_walk_ <= o.time_walk_;
    }
  }

  std::uint16_t time_walk_;
};

#elifdef TB_TRANSFER_CLASS

struct journey_transfer_class : journey {

  bool dominates(journey_transfer_class const& o) const {
    if (start_time_ <= dest_time_) {
      return transfers_ <= o.transfers_ && start_time_ >= o.start_time_ &&
             dest_time_ <= o.dest_time_ &&
             transfer_class_max_ <= o.transfer_class_max_ &&
             transfer_class_sum_ <= o.transfer_class_sum_;
    } else {
      return transfers_ <= o.transfers_ && start_time_ <= o.start_time_ &&
             dest_time_ >= o.dest_time_ &&
             transfer_class_max_ <= o.transfer_class_max_ &&
             transfer_class_sum_ <= o.transfer_class_sum_;
    }
  }

  std::uint8_t transfer_class_max_;
  std::uint8_t transfer_class_sum_;
};

#endif

}  // namespace nigiri::routing::tripbased

#endif