#include "nigiri/repertoire_filter.h"

#include "utl/enumerate.h"
#include "utl/helpers/algorithm.h"

namespace nigiri {

namespace n = nigiri;

void repertoire_filter(std::vector<n::location_idx_t>& in,
                       std::vector<n::location_idx_t>& out,
                       geo::latlng const& pos,
                       n::timetable const& tt,
                       std::uint32_t const filter_threshold,
                       std::uint8_t const stations_per_route) {
  utl::sort(in, [&](auto const a, auto const b) {
    return geo::distance(pos, tt.locations_.coordinates_[a]) <
           geo::distance(pos, tt.locations_.coordinates_[b]);
  });

  auto repertoire = std::vector<std::uint8_t>{};
  repertoire.resize(tt.n_routes());
  for (auto const [i, l] : utl::enumerate(in)) {
    auto expands_repertoire = false;
    for (auto const r : tt.location_routes_[l]) {
      if (repertoire[r.v_] < stations_per_route) {
        expands_repertoire = true;
        ++repertoire[r.v_];
      }
    }
    if (i < filter_threshold || expands_repertoire) {
      out.emplace_back(l);
    }
  }
}

}  // namespace nigiri