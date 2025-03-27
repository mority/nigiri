#include "nigiri/repertoire_filter.h"

namespace nigiri {

namespace n = nigiri;

repertoire_filter::repertoire_filter(n::timetable const& tt)
    : tt_{tt}, repertoire_{tt.n_routes()} {}

void repertoire_filter::filter(std::vector<n::location_idx_t> const& sorted_in,
                               std::vector<n::location_idx_t>& out) {
  utl::fill(repertoire_.blocks_, 0U);
  for (auto const& l : sorted_in) {
    auto expands_repertoire = false;
    for (auto const r : tt_.location_routes_[l]) {
      if (!repertoire_.test(r.v_)) {
        expands_repertoire = true;
      }
      repertoire_.set(r.v_);
    }
    if (expands_repertoire) {
      out.emplace_back(l);
    }
  }
}

}  // namespace nigiri