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
  // preprocessing without reduction step
  void initial_transfer_computation(timetable& tt);

  // preprocessing with U-turn transfer removal
  void u_turn_transfer_removal(timetable& tt);

  // preprocessing with U-turn transfer removal and transfer reduction
  void transfer_reduction(timetable& tt);

  void build_transfer_set(timetable& tt) {
    initial_transfer_computation(tt);
  }

  // load precomputed transfer set from file
  // also needs to load the corresponding timetable from file since bitfields
  // of the transfers are stored in the timetable
  void load_transfer_set(/* file name */);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  transfer_set ts{};
};

}  // namespace nigiri::routing::tripbased