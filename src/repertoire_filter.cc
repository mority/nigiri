#include "nigiri/repertoire_filter.h"

#include "utl/enumerate.h"

namespace nigiri {

namespace n = nigiri;

constexpr auto const kStationsPerRoute = std::uint8_t{2U};

void repertoire_filter(std::vector<n::location_idx_t> const& sorted_in,
                       std::vector<n::location_idx_t>& out,
                       n::timetable const& tt) {
  auto repertoire = std::vector<std::uint8_t>{};
  repertoire.resize(tt.n_routes());
  for (auto const [i, l] : utl::enumerate(sorted_in)) {
    auto expands_repertoire = false;
    for (auto const r : tt.location_routes_[l]) {
      if (repertoire[r.v_] < kStationsPerRoute) {
        expands_repertoire = true;
      }
      ++repertoire[r.v_];
    }
    if (expands_repertoire) {
      out.emplace_back(l);
    }
  }
}

}  // namespace nigiri