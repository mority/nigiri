#include "nigiri/repertoire_filter.h"

namespace nigiri {

namespace n = nigiri;

void repertoire_filter(std::vector<n::location_idx_t> const& sorted_in,
                       std::vector<n::location_idx_t>& out,
                       n::timetable const& tt) {
  auto repertoire = bitvec{tt.n_routes()};
  for (auto const& l : sorted_in) {
    auto expands_repertoire = false;
    for (auto const r : tt.location_routes_[l]) {
      if (!repertoire.test(r.v_)) {
        expands_repertoire = true;
      }
      repertoire.set(r.v_);
    }
    if (expands_repertoire) {
      out.emplace_back(l);
    }
  }
}

}  // namespace nigiri