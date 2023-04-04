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

struct transfer_set {
  transfer_set() = default;

  void add(transfer const&);

  //
  transfer get_transfers(transport_idx_t const, location_idx_t const);

  using iterator_t = typename vector<transfer>::const_iterator;
  using index_t = std::uint64_t;
  // data
  struct entry {

    entry(vector<transfer> const& data, vector<index_t> const& index, index_t key)
        : data_(data),
          index_start_(index[key]),
          index_end_(index[key+1]),
          key_{key} {}



    vector<transfer> const& data_;
    index_t const index_start_;
    index_t const index_end_;
    index_t key_;
  };

  vector_map<transport_idx_t, std::uint32_t> transport_to_stops_;
  vector_map<location_idx_t, std::uint32_t> stop_to_transfers_;
  vector<transfer> transfers_;
};

} // namespace nigiri::routing::tripbased