#include "nigiri/loader/build_lb_routes.h"

#include <unordered_set>

#include "utl/enumerate.h"
#include "utl/helpers/algorithm.h"

namespace nigiri::loader {

static constexpr auto kUnset =
    lb_route_idx_t{std::numeric_limits<lb_route_idx_t::value_t>::max()};

void build_lb_routes(timetable& tt, profile_idx_t prf_idx) {
  auto route_lb_route = vector_map<route_idx_t, lb_route_idx_t>{};
  route_lb_route.resize(tt.n_routes());
  utl::fill(route_lb_route, kUnset);
  auto equivalence = std::vector<route_idx_t>{};
  auto lb_segments_layovers = std::vector<duration_t>{};

  for (auto const representative :
       interval{route_idx_t{0U}, route_idx_t{tt.n_routes()}}) {
    if (route_lb_route[representative] != kUnset) {
      continue;
    }

    equivalence.clear();
    equivalence.push_back(representative);
    for (auto const r :
         interval{representative + 1U, route_idx_t{tt.n_routes()}}) {
      if (route_lb_route[r] != kUnset) {
        continue;
      }
      if (utl::equal(tt.route_location_seq_[representative],
                     tt.route_location_seq_[r])) {
        equivalence.push_back(r);
      }
    }

    auto const n_segments = tt.route_location_seq_[representative].size() - 1U;
    auto const n_layovers = n_segments - 1U;
    lb_segments_layovers.resize(n_segments + n_layovers);
    utl::fill(lb_segments_layovers, duration_t::max());

    for (auto const e : equivalence) {
      for (auto const t : tt.route_transport_ranges_[e]) {
        for (auto s = stop_idx_t{0U};
             s != tt.route_location_seq_[e].size() - 1U; ++s) {
          if (s != stop_idx_t{0U}) {
            auto const layover =
                duration_t{(tt.event_mam(t, s, event_type::kDep) -
                            tt.event_mam(t, s, event_type::kArr))
                               .count()};
            lb_segments_layovers[2 * s - 1] =
                std::min(lb_segments_layovers[2 * s - 1], layover);
          }
          auto const segment =
              duration_t{(tt.event_mam(t, s + 1U, event_type::kArr) -
                          tt.event_mam(t, s, event_type::kDep))
                             .count()};
          lb_segments_layovers[2 * s] =
              std::min(lb_segments_layovers[2 * s], segment);
        }
      }
      route_lb_route[e] = lb_route_idx_t{tt.lb_route_route_[prf_idx].size()};
    }

    tt.lb_route_times_[prf_idx].emplace_back(lb_segments_layovers);
    tt.lb_route_route_[prf_idx].emplace_back(representative);
  }

  auto lb_routes = std::unordered_set<lb_route_idx_t>{};
  for (auto const l :
       interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
    lb_routes.clear();
    for (auto const r : tt.location_routes_[l]) {
      lb_routes.insert(route_lb_route[r]);
    }
    tt.location_lb_routes_[prf_idx].emplace_back(lb_routes);
  }
}
}  // namespace nigiri::loader