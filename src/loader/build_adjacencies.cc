#include "nigiri/loader/build_adjacencies.h"

#include <unordered_set>

#include "utl/enumerate.h"
#include "utl/parallel_for.h"
#include "utl/progress_tracker.h"

namespace nigiri::loader {

void build_adjacencies(timetable& tt, profile_idx_t const prf_idx) {
  auto const timer = scoped_timer{"loader.build_lb_adjacency"};

  struct durations {
    bool operator<(durations const o) const {
      return o.pt_duration_ == std::numeric_limits<std::uint16_t>::max() ||
             pt_duration_ + transfer_duration_ <
                 o.pt_duration_ + o.transfer_duration_;
    }

    std::uint16_t pt_duration_{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t transfer_duration_{std::numeric_limits<std::uint16_t>::max()};
  };

  struct adjacency_sets {
    std::unordered_set<location_idx_t> in_;
    std::unordered_set<location_idx_t> out_;
  };

  struct adjacencies {
    std::vector<location_idx_t> in_;
    std::vector<location_idx_t> out_;
  };

  auto const explore_routes = [&](location_idx_t const l, adjacency_sets& a) {
    for (auto const r : tt.location_routes_[l]) {
      if ((prf_idx == kCarProfile && !tt.has_car_transport(r)) ||
          (prf_idx == kBikeProfile && !tt.has_bike_transport(r))) {
        continue;
      }

      auto const location_seq = tt.route_location_seq_[r];
      for (auto const [i, x] : utl::enumerate(location_seq)) {
        auto const i_stop = stop{x};

        if (l != i_stop.location_idx()) {
          continue;
        }

        for (auto const [j, y] : utl::enumerate(location_seq)) {
          auto const j_stop = stop{y};

          if (l == j_stop.location_idx()) {
            continue;
          }

          if (j < i && j_stop.in_allowed() &&
              i_stop.out_allowed()) {  // TODO wheelchair profile
            a.in_.emplace(j_stop.location_idx());
            for (auto const fp :
                 tt.locations_.footpaths_in_[prf_idx][j_stop.location_idx()]) {
              a.in_.emplace(fp.target());
            }
          }

          if (i < j && i_stop.in_allowed() &&
              j_stop.out_allowed()) {  // TODO wheelchair profile
            a.out_.emplace(j_stop.location_idx());
            for (auto const fp :
                 tt.locations_.footpaths_out_[prf_idx][j_stop.location_idx()]) {
              a.out_.emplace(fp.target());
            }
          }
        }
      }
    }
  };

  auto const pt = utl::get_active_progress_tracker();
  pt->status("Computing adjacencies").in_high(tt.n_locations());
  utl::parallel_ordered_collect_threadlocal<adjacency_sets>(
      tt.n_locations(),
      // parallel
      [&](adjacency_sets& a, std::size_t const i) {
        auto const l = location_idx_t{i};
        a.in_.clear();
        a.out_.clear();
        auto ns = adjacencies{};

        explore_routes(l, a);

        for (auto const n : a.in_) {
          ns.in_.emplace_back(n);
        }
        for (auto const n : a.out_) {
          ns.out_.emplace_back(n);
        }

        return ns;
      },
      // ordered
      [&](std::size_t, adjacencies&& ns) {
        tt.fwd_search_adjacency_[prf_idx].emplace_back(ns.in_);
        tt.bwd_search_adjacency_[prf_idx].emplace_back(ns.out_);
      },
      pt->update_fn());
}

}  // namespace nigiri::loader