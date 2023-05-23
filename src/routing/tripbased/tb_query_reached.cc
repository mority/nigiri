#include "nigiri/routing/tripbased/tb_query_reached.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void reached::update(transport_idx_t const transport_idx,
                     std::uint32_t const stop_idx,
                     day_idx_t const day_idx,
                     std::uint32_t const num_transfers) {
  data_[transport_idx.v_].add(
      reached_entry{stop_idx, day_idx.v_, num_transfers});
}

std::uint16_t reached::query(transport_idx_t const transport_idx,
                             day_idx_t const day_idx,
                             std::uint32_t const num_transfers) {
  // no entry for this transport_idx/day_idx combination
  // return stop index of final stop of the transport
  auto stop_idx = static_cast<uint16_t>(
      tbp_.tt_.route_location_seq_[tbp_.tt_.transport_route_[transport_idx]]
          .size() -
      1);

  day_idx_t day_distance{kDayIdxMax};
  for (auto const& re : data_[transport_idx]) {
  }

  return stop_idx;
}

// earliest arrival query initial implementation
// void reached::update(transport_idx_t const transport_idx,
//                      std::uint16_t const stop_idx,
//                      day_idx_t const day_idx) {
//
//   auto day_idx_end = std::numeric_limits<day_idx_t>::max();
//
//   for (auto& re : data_[transport_idx]) {
//     if (re.day_idx_ == re.day_idx_end_) {
//       continue;
//     }
//     if (stop_idx < re.stop_idx_) {
//       if (day_idx <= re.day_idx_) {
//         re.day_idx_ = re.day_idx_end_;
//       } else if (day_idx < re.day_idx_end_) {
//         re.day_idx_end_ = day_idx.v_;
//       }
//     } else if (stop_idx == re.stop_idx_) {
//       if (day_idx < re.day_idx_) {
//         re.day_idx_ = re.day_idx_end_;
//       } else {
//         // new entry already covered by current entry
//         return;
//       }
//     } else if (day_idx < re.day_idx_) {
//       day_idx_end = re.get_day_idx_start();
//     } else {
//       // new entry already covered by current entry
//       return;
//     }
//   }
//
//   data_[transport_idx].emplace_back(stop_idx, day_idx.v_, day_idx_end.v_);
// }
//
// std::uint16_t reached::query(transport_idx_t const transport_idx,
//                              day_idx_t const day_idx) {
//
//   // look up stop index for the provided day index
//   // reverse iteration because newest entry is more likely to be valid
//   // newest entry is in the back
//   for (std::uint32_t i =
//            static_cast<std::uint32_t>(data_[transport_idx].size());
//        i-- > 0;) {
//     auto const& re = &data_[transport_idx][i];
//     if (re->get_day_idx_start() <= day_idx && day_idx <
//     re->get_day_idx_end()) {
//       return re->stop_idx_;
//     }
//   }
//
//   // no entry for this transport_idx/day_idx combination
//   // return stop index of final stop of the transport
//   return static_cast<uint16_t>(
//       tbp_.tt_.route_location_seq_[tbp_.tt_.transport_route_[transport_idx]]
//           .size() -
//       1);
// }