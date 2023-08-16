#pragma once

#include "nigiri/routing/tripbased/settings.h"

#ifdef TB_TRANSFER_CLASS

namespace nigiri::routing::tripbased {

static inline std::uint8_t transfer_class(std::uint16_t waiting_time) {
  if (waiting_time > TB_TRANSFER_CLASS0) {
    return 0;
  } else if (waiting_time > TB_TRANSFER_CLASS1) {
    return 1;
  } else {
    return 2;
  }
}

static inline std::uint16_t transfer_wait(timetable const& tt,
                                          transport_idx_t const& t,
                                          stop_idx_t const& i,
                                          transport_idx_t const& u,
                                          stop_idx_t const& j,
                                          std::uint16_t const& d_t,
                                          std::uint16_t const& d_u) {
  auto const p_t_i =
      stop{tt.route_location_seq_[tt.transport_route_[t]][i]}.location_idx();
  auto const p_u_j =
      stop{tt.route_location_seq_[tt.transport_route_[u]][j]}.location_idx();
  std::uint16_t walk_or_change = 0U;
  // determine time required for transfer
  if (p_t_i == p_u_j) {
    walk_or_change = tt.locations_.transfer_time_[p_t_i].count();
  } else {
    for (auto const& fp : tt.locations_.footpaths_out_[p_t_i]) {
      if (fp.target() == p_u_j) {
        walk_or_change = fp.duration_;
        break;
      }
    }
  }
  // calculate waiting time
  return d_u * 1440U +
         static_cast<std::uint16_t>(
             tt.event_mam(u, j, event_type::kDep).count()) -
         (d_t * 1440U +
          static_cast<std::uint16_t>(
              tt.event_mam(t, i, event_type::kArr).count()) +
          walk_or_change);
}

}  // namespace nigiri::routing::tripbased

#endif