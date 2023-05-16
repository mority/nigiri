#pragma once

#include <cmath>

#include "nigiri/types.h"

#define TBDL std::cerr << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

#define BITFIELD_IDX_BITS 25U
#define TRANSPORT_IDX_BITS 26U
#define STOP_IDX_BITS 11U
#define DAY_IDX_BITS 10U

namespace nigiri::routing::tripbased {

constexpr unsigned const kBitfieldIdxMax = 1U << BITFIELD_IDX_BITS;
constexpr unsigned const kTransportIdxMax = 1U << TRANSPORT_IDX_BITS;
constexpr unsigned const kStopIdxMax = 1U << STOP_IDX_BITS;
constexpr unsigned const kDayIdxMax = 1U << DAY_IDX_BITS;

struct transfer {
  transfer(bitfield_idx_t const bitfield_idx,
           transport_idx_t const transport_idx_to,
           std::uint16_t const stop_idx_to,
           day_idx_t const passes_midnight)
      : bitfield_idx_(bitfield_idx.v_),
        transport_idx_to_(transport_idx_to.v_),
        stop_idx_to_(stop_idx_to),
        passes_midnight_(passes_midnight.v_) {}

  // the days on which the transfer can take place
  bitfield_idx_t get_bitfield_idx() const {
    return bitfield_idx_t{bitfield_idx_};
  }

  // the transport that is the target of the transfer
  transport_idx_t get_transport_idx_to() const {
    return transport_idx_t{transport_idx_to_};
  }

  // the stop index of the target transport
  std::uint16_t get_stop_idx_to() const {
    return static_cast<std::uint16_t>(stop_idx_to_);
  }

  // bit: 1 -> the transfer passes midnight
  day_idx_t get_passes_midnight() const { return day_idx_t{passes_midnight_}; }

  // the days on which the transfer can take place
  std::uint64_t const bitfield_idx_ : BITFIELD_IDX_BITS;

  // the transport that is the target of the transfer
  std::uint64_t const transport_idx_to_ : TRANSPORT_IDX_BITS;

  // the stop index of the target transport
  std::uint64_t const stop_idx_to_ : STOP_IDX_BITS;

  // bit: 1 -> the transfer passes midnight
  std::uint64_t const passes_midnight_ : 1;
};

struct hash_transfer_set {
  using key = pair<transport_idx_t, unsigned>;
  using entry = pair<std::uint32_t, std::uint32_t>;

  void add(transport_idx_t const& transport_idx_from,
           std::uint32_t const stop_idx_from,
           transport_idx_t const& transport_idx_to,
           std::uint32_t const stop_idx_to,
           bitfield_idx_t const& bitfield_idx,
           day_idx_t passes_midnight);

  void finalize();

  // returns entry containing start index and end index (exclusive) of transfers
  // for given transport at given stop
  std::optional<entry> get_transfers(transport_idx_t const&,
                                     std::uint32_t const stop_idx);

  transfer& operator[](std::uint32_t index) { return transfers_[index]; }

  std::size_t size() const { return transfers_.size(); }

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