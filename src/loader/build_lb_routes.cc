#include "nigiri/loader/build_lb_routes.h"

#include "boost/geometry/algorithms/detail/envelope/interface.hpp"

#include <unordered_set>

#include "utl/helpers/algorithm.h"
#include "utl/progress_tracker.h"
#include "utl/zip.h"

namespace nigiri::loader {

static constexpr auto kUnset =
    lb_route_idx_t{std::numeric_limits<lb_route_idx_t::value_t>::max()};

void build_lb_routes(timetable& tt, profile_idx_t prf_idx) {
  auto const timer = scoped_timer{"loader.build_lb_routes"};

  tt.location_lb_routes_[prf_idx].clear();
  tt.lb_route_root_seq_[prf_idx].clear();
  tt.lb_route_times_[prf_idx].clear();

  auto route_lb_route = vector_map<route_idx_t, lb_route_idx_t>{};
  route_lb_route.resize(tt.n_routes());
  utl::fill(route_lb_route, kUnset);

  auto root_seq = std::vector<location_idx_t>{};
  auto const set_root_seq = [&](route_idx_t const r) {
    root_seq.clear();
    for (auto const s : tt.route_location_seq_[r]) {
      root_seq.push_back(tt.locations_.get_root_idx(stop{s}.location_idx()));
    }
  };

  auto equivalence = std::vector<route_idx_t>{};
  auto const set_equivalence = [&](route_idx_t const representative) {
    equivalence.clear();

    auto const add = [&](route_idx_t const r) {
      equivalence.push_back(r);
      route_lb_route[r] = lb_route_idx_t{tt.lb_route_times_[prf_idx].size()};
    };

    add(representative);

    auto const equal_root_stops = [&](auto const r) {
      auto const& seq = tt.route_location_seq_[r];
      if (root_seq.size() != seq.size()) {
        return false;
      }
      for (auto const [i, j] : utl::zip(root_seq, seq)) {
        if (i != tt.locations_.get_root_idx(stop{j}.location_idx())) {
          return false;
        }
      }
      return true;
    };

    for (auto const r :
         interval{representative + 1U, route_idx_t{tt.n_routes()}}) {
      if (route_lb_route[r] != kUnset ||
          (prf_idx == kCarProfile && !tt.has_car_transport(r)) ||
          (prf_idx == kBikeProfile && !tt.has_bike_transport(r))) {
        continue;
      }
      if (equal_root_stops(r)) {
        equivalence.push_back(r);
      }
    }
  };

  auto lb_segments_layovers = std::vector<duration_t>{};

  auto const pt = utl::get_active_progress_tracker();
  pt->status("Compute lower bound routes").in_high(tt.n_locations());
  for (auto const representative :
       interval{route_idx_t{0U}, route_idx_t{tt.n_routes()}}) {
    if (route_lb_route[representative] != kUnset ||
        (prf_idx == kCarProfile && !tt.has_car_transport(representative)) ||
        (prf_idx == kBikeProfile && !tt.has_bike_transport(representative))) {
      continue;
    }

    set_root_seq(representative);
    set_equivalence(representative);

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
      pt->increment();
    }

    tt.lb_route_times_[prf_idx].emplace_back(lb_segments_layovers);
    tt.lb_route_root_seq_[prf_idx].emplace_back(root_seq);
  }

  auto lb_routes = std::unordered_set<lb_route_idx_t>{};
  auto const add_lb_routes = [&](auto const l) {
    for (auto const r : tt.location_routes_[l]) {
      if (route_lb_route[r] != kUnset) {
        lb_routes.insert(route_lb_route[r]);
      }
    }
  };

  for (auto const l :
       interval{location_idx_t{0U}, location_idx_t{tt.n_locations()}}) {
    lb_routes.clear();

    if (tt.locations_.parents_[l] == location_idx_t::invalid()) {
      add_lb_routes(l);
      for (auto const c : tt.locations_.children_[l]) {
        add_lb_routes(c);
        for (auto const cc : tt.locations_.children_[c]) {
          add_lb_routes(cc);
        }
      }
    }

    tt.location_lb_routes_[prf_idx].emplace_back(lb_routes);
  }

  log(log_lvl::info, "nigiri.loader.lb_routes",
      "Merged {} routes into {} LB routes", tt.n_routes(),
      tt.lb_route_times_[prf_idx].size());
}

}  // namespace nigiri::loader