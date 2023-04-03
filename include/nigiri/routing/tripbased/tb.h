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
  bitfield bitfield_from_;
  std::int8_t shift_amount_to_;

  // walking time: from stop -> to stop
  duration_t walking_time(location_idx_t const from, location_idx_t const to);
};

bool operator <(const transfer& t1, const transfer& t2) {
  return std::tie(t1.transport_idx_from_, t1.location_idx_from_) < std::tie(t2.transport_idx_from_, t2.location_idx_from_);
}

struct transfer_set {
  // add the transfer to the end of the transfer set, without sorting
  // requires sorting after transfer computation
  void emplace_back(const transfer&);

  void insert(const transfer_idx_t, const transfer&);

  // add the transfer while maintaining the sorting
  void add_sorted(const transfer&);

  transfer_idx_t find_insertion_idx(const transfer&);

  // sort the transfer set by
  // primary: transport_idx_from
  // secondary: location_idx_from
  void sort();

  // construct an indexing structure: transport_from_idx -> first transfer
  void index();

  // get the transfer for this transfer index from the transfer set
  transfer get(transfer_idx_t const);

  // map a transport_idx to its first transfer in the sorted transfer set
  hash_map<transport_idx_t,transfer_idx_t> transport_to_transfer_idx_;

  // attributes of the transfers
  vector_map<transfer_idx_t, transport_idx_t> transports_from_;
  vector_map<transfer_idx_t, location_idx_t> locations_from_;
  vector_map<transfer_idx_t, transport_idx_t> transports_to_;
  vector_map<transfer_idx_t, location_idx_t> locations_to_;
  vector_map<transfer_idx_t, bitfield> bitfields_from_;
  vector_map<transfer_idx_t, std::int8_t> shift_amounts_to_;
};

} // namespace nigiri::routing::tripbased