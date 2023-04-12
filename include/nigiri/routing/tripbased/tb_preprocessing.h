#pragma once

#include "nigiri/timetable.h"
#include "tb.h"

namespace nigiri::routing::tripbased {

struct earliest_times {
  struct et_entry {
    minutes_after_midnight_t time;
    bitfield bitfield_;
  };

  vector<pair<minutes_after_midnight_t,bitfield>> data_;
};

struct tb_preprocessing {
  tb_preprocessing() = delete;
  tb_preprocessing(timetable& tt) : tt_(tt) {}

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
  hash_transfer_set ts{};
};

}  // namespace nigiri::routing::tripbased