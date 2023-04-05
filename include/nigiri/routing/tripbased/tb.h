#pragma once

#include "../../types.h"

namespace nigiri::routing::tripbased {

// transfer: trip t, stop p -> trip u, stop q
// DoOBS for trip t and u: instances of trip t for which transfer is possible
// from is implicit -> given by indexing of transfer set
// via footpath -> can be accessed by location idx of from and to
struct transfer {
  transfer() = default;
  transfer(transport_idx_t transport_idx_to, location_idx_t location_idx_to, bitfield_idx_t bitfield_idx_from, bitfield_idx_t bitfield_idx_to)
      : transport_idx_to_(transport_idx_to), location_idx_to_(location_idx_to), bitfield_idx_from_(bitfield_idx_from), bitfield_idx_to_(bitfield_idx_to) {}

  // to
  transport_idx_t transport_idx_to_{};
  location_idx_t location_idx_to_{};

  // bit set to mark instances of trips
  bitfield_idx_t bitfield_idx_from_{};
  bitfield_idx_t bitfield_idx_to_{};
};


using key = pair<transport_idx_t,location_idx_t>;
using entry = pair<std::uint64_t,std::uint64_t>;

struct transfer_set {
  void add(transport_idx_t const, location_idx_t const, transfer const&);

  void finalize();

  // returns entry containing start index and end index (exclusive) of transfers
  // for given transport at given stop
  std::optional<entry> get_transfers(transport_idx_t const, location_idx_t const);

  transfer& operator [](std::uint64_t index) {
    return transfers_[index];
  }

  bool initialized_ = false;
  bool finalized_ = false;
  hash_map<key,entry> index_{};
  vector<transfer> transfers_{};
  transport_idx_t cur_transport_idx_from_{};
  location_idx_t cur_location_idx_from_{};
  std::uint64_t cur_start_ = 0U;
  std::uint16_t cur_length_ = 0U;

};

} // namespace nigiri::routing::tripbased