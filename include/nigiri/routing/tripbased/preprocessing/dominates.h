#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "ordered_transport_id.h"

namespace nigiri::routing::tripbased {

#ifdef TB_MIN_WALK
#ifdef TB_PREPRO_LB_PRUNING
static inline std::int32_t dominates(std::uint32_t otid1,
                                     std::int32_t tw1,
                                     std::uint32_t otid2,
                                     std::int32_t tw2) {
  auto const difa =
      static_cast<std::int64_t>(otid1) - static_cast<std::int64_t>(otid2);
  auto const sgna = (0 < difa) - (difa < 0);
  auto const difw = tw1 - tw2;
  auto const sgnw = (0 < difw) - (difw < 0);
  return sgna + sgnw;
}
#else
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
#endif

#ifdef TB_TRANSFER_CLASS
static inline std::int32_t dominates(std::int32_t tau1,
                                     std::uint8_t kappa1,
                                     std::int32_t tau2,
                                     std::uint8_t kappa2) {
  auto const dif_tau = tau1 - tau2;
  auto const sgn_tau = (0 < dif_tau) - (dif_tau < 0);
  auto const dif_kappa =
      static_cast<std::int16_t>(kappa1) - static_cast<std::int16_t>(kappa2);
  auto const sgn_kappa = (0 < dif_kappa) - (dif_kappa < 0);
  return sgn_tau + sgn_kappa;
}
#ifdef TB_PREPRO_LB_PRUNING
static inline std::int32_t dominates(std::uint32_t otid1,
                                     std::int8_t kappa1,
                                     std::uint32_t otid2,
                                     std::int8_t kappa2) {
  auto const difa =
      static_cast<std::int64_t>(otid1) - static_cast<std::int64_t>(otid2);
  auto const sgna = (0 < difa) - (difa < 0);
  auto const difk = kappa1 - kappa2;
  auto const sgnk = (0 < difk) - (difk < 0);
  return sgna + sgnk;
}
#endif
#endif

}  // namespace nigiri::routing::tripbased