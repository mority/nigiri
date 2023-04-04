#pragma once

#include "tb.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

struct tb_preprocessing {
  // build transfer set from timetable

  // load precomputed transfer set from file

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};
};

}  // namespace nigiri::routing::tripbased