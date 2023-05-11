#pragma once

#include "nigiri/types.h"

#define TBDL std::cerr << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

namespace nigiri::routing::tripbased {

struct transfer {
  transfer() = default;
  transfer(bitfield_idx_t const bitfield_idx,
           transport_idx_t const transport_idx_to,
           unsigned const stop_idx_to,
           std::uint32_t const passes_midnight)
      : bitfield_idx_(bitfield_idx),
        transport_idx_to_(transport_idx_to),
        stop_idx_to_(stop_idx_to),
        passes_midnight_(passes_midnight) {}

  // the days on which the transfer can take place
  bitfield_idx_t const bitfield_idx_{};

  // the transport that is the target of the transfer
  transport_idx_t const transport_idx_to_{};

  // the stop index of the target transport
  std::uint32_t const stop_idx_to_ : 31 {};

  // bit: 1 -> the transfer passes midnight
  std::uint32_t const passes_midnight_ : 1 {};
};

struct hash_transfer_set {
  using key = pair<transport_idx_t, unsigned>;
  using entry = pair<std::uint32_t, std::uint32_t>;

  void add(transport_idx_t const& transport_idx_from,
           std::uint32_t const stop_idx_from,
           transport_idx_t const& transport_idx_to,
           std::uint32_t const stop_idx_to,
           bitfield_idx_t const& bitfield_idx,
           std::uint32_t passes_midnight);

  void finalize();

  // returns entry containing start index and end index (exclusive) of transfers
  // for given transport at given stop
  std::optional<entry> get_transfers(transport_idx_t const&,
                                     std::uint32_t const stop_idx);

  transfer& operator[](std::uint32_t index) { return transfers_[index]; }

  bool initialized_ = false;
  bool finalized_ = false;

  transport_idx_t cur_transport_idx_from_{};
  std::uint32_t cur_stop_idx_from_;
  std::uint32_t cur_start_ = 0U;
  std::uint32_t cur_length_ = 0U;

  hash_map<key, entry> index_{};
  vector<transfer> transfers_{};
};

// returns the number of times midnight is passed
constexpr day_idx_t num_midnights(duration_t const& d) {
  return day_idx_t{d.count() / 1440};
}

// returns the time of day given a duration that starts at midnight
constexpr duration_t time_of_day(duration_t const& d) {
  return duration_t(d.count() % 1440);
}

}  // namespace nigiri::routing::tripbased