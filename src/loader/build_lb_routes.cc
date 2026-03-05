#include "nigiri/loader/build_lb_routes.h"

#include "utl/enumerate.h"
#include "utl/helpers/algorithm.h"

namespace nigiri::loader {

static constexpr auto kUnset =
    lb_route_idx_t{std::numeric_limits<lb_route_idx_t::value_t>::max()};

void build_lb_routes(timetable& tt, profile_idx_t prf_idx) {
  auto route_lb_route = vector_map<route_idx_t, lb_route_idx_t>{};
  route_lb_route.resize(tt.n_routes());
  utl::fill(route_lb_route, kUnset);
  auto equivalences = std::vector<route_idx_t>{};
  auto lb_segment_times = std::vector<duration_t>{};

  for (auto const r0 : interval{route_idx_t{0U}, route_idx_t{tt.n_routes()}}) {
    if (route_lb_route[r0] != kUnset) {
      continue;
    }

    equivalences.clear();
    equivalences.push_back(r0);
    for (auto const r : interval{r0 + 1U, route_idx_t{tt.n_routes()}}) {
      if (route_lb_route[r] != kUnset) {
        continue;
      }
      if (utl::equal(tt.route_location_seq_[r0], tt.route_location_seq_[r])) {
        equivalences.push_back(r);
      }
    }

    lb_segment_times.resize(tt.route_location_seq_[r0].size() + 1U);
    utl::fill(lb_segment_times, duration_t::max());
    for (auto const e : equivalences) {
      for (auto const t : tt.route_transport_ranges_[e]) {
        for (auto const [i, l] : utl::enumerate(tt.route_location_seq_[e])) {
          auto const is_first = i == 0U;
          auto const is_last = i == tt.route_location_seq_[e].size() - 1U;

          if (!is_last) {
          }
          if (!is_first) {
          }
        }
      }
      route_lb_route[e] = lb_route_idx_t{tt.lb_route_times_[prf_idx].size()};
    }
  }
}

}  // namespace nigiri::loader