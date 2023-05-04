#pragma once

#include "nigiri/types.h"

#define TBDL std::cerr << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

namespace nigiri::routing::tripbased {

struct transfer {
  transfer() = default;
  transfer(transport_idx_t const transport_idx_to,
           unsigned const stop_idx_to,
           bitfield_idx_t const bitfield_idx,
           int const shift_amount)
      : transport_idx_to_(transport_idx_to),
        stop_idx_to_(stop_idx_to),
        bitfield_idx_(bitfield_idx),
        shift_amount_(shift_amount) {}

  // to
  transport_idx_t const transport_idx_to_{};
  unsigned const stop_idx_to_{};

  // bitfield to mark instances of transport
  bitfield_idx_t const bitfield_idx_{};
  // how far into the future/past the bitfield has to be shifted to mark
  // instances of the target transport of the transfer
  int const shift_amount_{};
};

struct hash_transfer_set {
  using key = pair<transport_idx_t, unsigned>;
  using entry = pair<std::uint32_t, std::uint32_t>;

  void add(transport_idx_t const& transport_idx_from,
           unsigned const stop_idx_from,
           transport_idx_t const& transport_idx_to,
           unsigned const stop_idx_to,
           bitfield_idx_t const& bitfield_idx_from,
           int shift_amount);

  void finalize();

  // returns entry containing start index and end index (exclusive) of transfers
  // for given transport at given stop
  std::optional<entry> get_transfers(transport_idx_t const&,
                                     unsigned const stop_idx);

  transfer& operator[](std::uint32_t index) { return transfers_[index]; }

  bool initialized_ = false;
  bool finalized_ = false;

  transport_idx_t cur_transport_idx_from_{};
  unsigned cur_stop_idx_from_;
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