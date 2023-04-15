#pragma once

#include "nigiri/timetable.h"
#include "tb.h"

namespace nigiri::routing::tripbased {

struct tb_preprocessing {
  tb_preprocessing() = delete;

  /// Constructor
  /// \param tt reference to timetable
  /// \param lh look-ahed, maximum time to wait for connection
  explicit tb_preprocessing(timetable& tt, duration_t lh = duration_t(1440U)) : tt_(tt), lh_(lh) {}

  // preprocessing without reduction step
  void initial_transfer_computation();

  // preprocessing with U-turn transfer removal
  void u_turn_transfer_removal();

  // preprocessing with U-turn transfer removal and transfer reduction
  void transfer_reduction();

  void build_transfer_set() {
    initial_transfer_computation();
  }

  // load precomputed transfer set from file
  // also needs to load the corresponding timetable from file since bitfields
  // of the transfers are stored in the timetable
  void load_transfer_set(/* file name */);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  bitfield_idx_t get_or_create_bfi(bitfield const& bf);

  timetable& tt_;
  duration_t lh_; // look-ahead
  hash_transfer_set ts_{};
};

}  // namespace nigiri::routing::tripbased