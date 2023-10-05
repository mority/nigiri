#pragma once

#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/routing/tripbased/transfer_set.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing::tripbased {

struct extracted_transfer {
  extracted_transfer(std::uint32_t const& transport_idx_to,
                     std::uint16_t const& stop_idx_to,
                     std::uint16_t const& passes_midnight)
      : transport_idx_to_(transport_idx_to),
        stop_idx_to_(stop_idx_to),
        passes_midnight_(passes_midnight) {}

  // the transport that is the target of the transfer
  transport_idx_t get_transport_idx_to() const {
    return transport_idx_t{transport_idx_to_};
  }

  // the stop index of the target transport
  stop_idx_t get_stop_idx_to() const {
    return static_cast<std::uint16_t>(stop_idx_to_);
  }

  // bit: 1 -> the transfer passes midnight
  day_idx_t get_passes_midnight() const { return day_idx_t{passes_midnight_}; }

  // the transport that is the target of the transfer
  std::uint64_t transport_idx_to_ : TRANSPORT_IDX_BITS;

  // the stop index of the target transport
  std::uint64_t stop_idx_to_ : STOP_IDX_BITS;

  // bit: 1 -> the transfer passes midnight
  std::uint64_t passes_midnight_ : 1;
};

struct extracted_transfer_set {
  extracted_transfer_set(timetable const& tt,
                         transfer_set const& ts,
                         day_idx_t d) {
    std::vector<std::vector<extracted_transfer>> stop_transfers;
    stop_transfers.resize(ts.route_max_length_);
    for (transport_idx_t transport_idx{0U}; transport_idx < ts.data_.size();
         ++transport_idx) {
      // collect active transfers
      for (stop_idx_t stop_idx{0U}; stop_idx < ts.data_.size(transport_idx.v_);
           ++stop_idx) {
        for (auto const& transfer : ts.data_.at(transport_idx.v_, stop_idx)) {
          auto const sigma =
              tt.event_mam(transport_idx, stop_idx, event_type::kArr).days_;
          auto const& beta = tt.bitfields_[transfer.get_bitfield_idx()];
          if (beta.test(d.v_ - sigma)) {
            stop_transfers[stop_idx].emplace_back(transfer.transport_idx_to_,
                                                  transfer.stop_idx_to_,
                                                  transfer.passes_midnight_);
          }
        }
      }
      // add collected transfers to extracted transfer set
      data_.emplace_back(
          it_range{stop_transfers.cbegin(),
                   stop_transfers.cbegin() +
                       static_cast<std::int64_t>(stop_transfers.size())});
      // clean up
      for (auto stop_idx{0U}; stop_idx < ts.data_.size(transport_idx.v_);
           ++stop_idx) {
        stop_transfers[stop_idx].clear();
      }
    }
  }

  nvec<std::uint32_t, extracted_transfer, 2> data_;
};

}  // namespace nigiri::routing::tripbased
