#pragma once

#include "nigiri/routing/tripbased/bits.h"

namespace nigiri::routing::tripbased {

#ifdef TB_MIN_WALK
static inline std::int32_t dominates(std::int32_t ta1,
                                     std::int32_t tw1,
                                     std::int32_t ta2,
                                     std::int32_t tw2) {
  auto const difa = ta1 - ta2;
  auto const sgna = (0 < difa) - (difa < 0);
  auto const difw = tw1 - tw2;
  auto const sgnw = (0 < difw) - (difw < 0);
  return sgna + sgnw;
}
#endif

}  // namespace nigiri::routing::tripbased