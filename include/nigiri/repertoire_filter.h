#pragma once

#include <vector>

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri {

struct repertoire_filter {
  repertoire_filter(nigiri::timetable const& tt);

  void filter(std::vector<nigiri::location_idx_t> const& sorted_in,
              std::vector<nigiri::location_idx_t>& out);

  nigiri::timetable const& tt_;
  bitvec repertoire_;
};

}  // namespace nigiri