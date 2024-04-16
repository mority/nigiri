#include "nigiri/routing/adhoc_transfer_patterns/adhoc_transfer_patterns.h"

namespace nigiri::routing {

template <class... Ts>
struct overloads : Ts... {
  using Ts::operator()...;
};

struct adhoc_transfer_patterns {
  adhoc_transfer_patterns(timetable const& tt, pareto_set<journey>& results)
      : tt_{tt} {
    roots_.reserve(100);
    for (auto const& j : results) {
      for (auto i = 0U; i != j.legs_.size(); ++i) {
        if (i == 0U) {
          roots_.emplace_back(j.legs_[i].from_);
        }

        auto use_run_enter_exit = [&](journey::run_enter_exit const& ree) {
          return kRide;
        };
        auto use_footpath = [](footpath const& fp) { return kWalk; };
        auto use_offset = [](offset const& o) { return kMUMO; };

        edges_[j.legs_[i].from_].emplace_back(
            j.legs_[i].to_,
            duration_t{j.legs_[i].arr_time_ - j.legs_[i].dep_time_},
            std::visit(overloads{use_run_enter_exit, use_footpath, use_offset},
                       j.legs_[i].uses_));
      }
    }
  }

  void journeys_from_patterns(pareto_set<journey>& results,
                              unixtime_t start_time) {
    for (auto const root : roots_) {
      auto cur_loc = root;
      while (!edges_[cur_loc].empty()) {
      }
    }
  }

private:
  enum edge_type { kRide, kWalk, kMUMO };

  struct edge {
    location_idx_t target_;
    duration_t duration_;
    edge_type type_;
  };

  timetable const& tt_;

  // transfer patterns graph
  std::vector<location_idx_t> roots_;
  std::vector<unixtime_t> arr_times_;
  mutable_fws_multimap<location_idx_t, edge> edges_;
};

void journeys_from_patterns(timetable const& tt,
                            pareto_set<journey>& results,
                            unixtime_t start_time) {
  // use adhoc_transfer_patterns struct to generate journeys that start after
  // the specified start time
}

}  // namespace nigiri::routing