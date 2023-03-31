#pragma once

#include "../../types.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing::tripbased {

// transfer: trip t, stop p -> trip u, stop q
// DoOBS for trip t: instances of trip t for which transfer is possible
// shift amount sigma_u: shift DoOBs for trip t -> DoOBS for trip u
struct transfer {
  // from
  transport_idx_t transport_idx_from;
  location_idx_t location_idx_from;

  // via

  // to
  transport_idx_t transport_idx_to;
  location_idx_t location_idx_to;

  // bit set to mark instances of trips

};

struct transfer_set {

};

struct tb_preprocessing {
  // build transfer set from timetable

  // load precomputed transfer set from file


};

}  // namespace nigiri::routing::tripbased