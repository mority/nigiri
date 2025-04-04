#include "nigiri/repertoire_filter.h"

#include "utl/enumerate.h"
#include "utl/helpers/algorithm.h"

namespace nigiri {

namespace n = nigiri;

void repertoire_filter(std::vector<n::location_idx_t>& sorted_in,
                       std::vector<n::location_idx_t>& out,
                       n::timetable const& tt,
                       std::uint8_t const stations_per_route) {
  auto repertoire = std::vector<std::uint8_t>{};
  repertoire.resize(tt.n_routes());
  for (auto const l : sorted_in) {
    auto expands_repertoire = false;
    for (auto const r : tt.location_routes_[l]) {
      if (repertoire[r.v_] < stations_per_route) {
        expands_repertoire = true;
        ++repertoire[r.v_];
      }
    }
    if (expands_repertoire) {
      out.emplace_back(l);
    }
  }
}

}  // namespace nigiri