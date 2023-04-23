#pragma once

#include "nigiri/types.h"

#define TBDL std::cerr << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

namespace nigiri::routing::tripbased {

// transfer: trip t, stop p -> trip u, stop q
// DoOBS for trip t and u: instances of trip t for which transfer is possible
// from is implicit -> given by indexing of transfer set
// via footpath -> can be accessed by location idx of from and to
struct transfer {
  transfer() = default;
  transfer(transport_idx_t transport_idx_to,
           location_idx_t location_idx_to,
           bitfield_idx_t bitfield_idx_from,
           bitfield_idx_t bitfield_idx_to)
      : transport_idx_to_(transport_idx_to),
        location_idx_to_(location_idx_to),
        bitfield_idx_from_(bitfield_idx_from),
        bitfield_idx_to_(bitfield_idx_to) {}

  // to
  transport_idx_t transport_idx_to_{};
  location_idx_t location_idx_to_{};

  // bitfields to mark instances of transport
  bitfield_idx_t bitfield_idx_from_{};
  bitfield_idx_t bitfield_idx_to_{};
};

struct hash_transfer_set {
  using key = pair<transport_idx_t, location_idx_t>;
  using entry = pair<std::uint32_t, std::uint32_t>;

  void add(transport_idx_t const& transport_idx_from,
           location_idx_t const& location_idx_from,
           transport_idx_t const& transport_idx_to,
           location_idx_t const& location_idx_to,
           bitfield_idx_t const& bitfield_idx_from,
           bitfield_idx_t const& bitfield_idx_to);

  void finalize();

  // returns entry containing start index and end index (exclusive) of transfers
  // for given transport at given stop
  std::optional<entry> get_transfers(transport_idx_t const&,
                                     location_idx_t const&);

  transfer& operator[](std::uint32_t index) { return transfers_[index]; }

  bool initialized_ = false;
  bool finalized_ = false;

  transport_idx_t cur_transport_idx_from_{};
  location_idx_t cur_location_idx_from_{};
  std::uint32_t cur_start_ = 0U;
  std::uint32_t cur_length_ = 0U;

  hash_map<key, entry> index_{};
  vector<transfer> transfers_{};
};

// returns the number of times midnight is passed
constexpr int num_midnights(duration_t const& d) {
  return int(d.count() / 1440);
}

// returns the time of day given a duration that starts at midnight
constexpr duration_t time_of_day(duration_t const& d) {
  return duration_t(d.count() % 1440);
}

}  // namespace nigiri::routing::tripbased