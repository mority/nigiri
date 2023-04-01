#pragma once

#include "../../types.h"

namespace nigiri::routing::tripbased {

using transfer_idx_t = cista::strong<std::uint64_t, struct _transfer_idx>;

// transfer: trip t, stop p -> trip u, stop q
// DoOBS for trip t: instances of trip t for which transfer is possible
// shift amount sigma_u: shift DoOBs for trip t -> DoOBS for trip u
struct transfer {
  // from
  transport_idx_t transport_idx_from_;
  location_idx_t location_idx_from_;

  // via footpath
  // -> can be accessed by location idx of from and to

  // to
  transport_idx_t transport_idx_to_;
  location_idx_t location_idx_to_;

  // bit set to mark instances of trips
  bitfield& bitfield_from_;
  std::int8_t shift_amount_to_;

  // walking time: from stop -> to stop
  duration_t walking_time(location_idx_t const from, location_idx_t const to);
};

struct transfer_set {
  void add(transfer&& t) {
    transports_from_.emplace_back(t.transport_idx_from_);
    locations_from_.emplace_back(t.location_idx_from_);
    transports_to_.emplace_back(t.transport_idx_to_);
    locations_to_.emplace_back(t.location_idx_to_);
    bitfields_from_.emplace_back(t.bitfield_from_);
    shift_amounts_to_.emplace_back(t.shift_amount_to_);

    assert(transports_from_.size() == locations_from_.size());
    assert(transports_from_.size() == transports_to_.size());
    assert(transports_from_.size() == locations_to_.size());
    assert(transports_from_.size() == bitfields_from_.size());
    assert(transports_from_.size() == shift_amounts_to_.size());
  }

  transfer get(transfer_idx_t const transfer_idx) {
    return transfer{transports_from_[transfer_idx],
          locations_from_[transfer_idx],
    transports_to_[transfer_idx],
    locations_to_[transfer_idx],
    bitfields_from_[transfer_idx],
    shift_amounts_to_[transfer_idx]};
  }

  // sort the transfer set by
  // primary: transport_idx_from
  // secondary: location_idx_from
  void sort();

  // add some sort of index structure to map a transport_idx to the first transfer
  // in the sorted transfer set

  vector_map<transfer_idx_t, transport_idx_t> transports_from_;
  vector_map<transfer_idx_t, location_idx_t> locations_from_;

  vector_map<transfer_idx_t, transport_idx_t> transports_to_;
  vector_map<transfer_idx_t, location_idx_t> locations_to_;

  vector_map<transfer_idx_t, bitfield> bitfields_from_;
  vector_map<transfer_idx_t, std::int8_t> shift_amounts_to_;
};

} // namespace nigiri::routing::tripbased