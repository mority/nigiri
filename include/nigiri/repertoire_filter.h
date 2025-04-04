#pragma once

#include <vector>

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri {

template <typename T>
void repertoire_filter(
    std::vector<T>& sorted_in,
    std::vector<T>& sorted_out,
    nigiri::timetable const& tt,
    std::uint8_t stations_per_route,
    std::function<location_idx_t(const T&)> get_loc = std::identity{}) {
  auto repertoire = std::vector<std::uint8_t>{};
  repertoire.resize(tt.n_routes());
  for (auto const e : sorted_in) {
    auto expands_repertoire = false;
    for (auto const r : tt.location_routes_[get_loc(e)]) {
      if (repertoire[r.v_] < stations_per_route) {
        expands_repertoire = true;
        ++repertoire[r.v_];
      }
    }
    if (expands_repertoire) {
      sorted_out.emplace_back(e);
    }
  }
}

}  // namespace nigiri