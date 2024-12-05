#pragma once

#include <map>
#include <vector>

#include "nigiri/routing/n_to_all/n_to_all.h"
#include "nigiri/routing/start_times.h"

namespace nigiri::routing {

struct n_to_all_search_state {
  n_to_all_search_state() = default;
  n_to_all_search_state(n_to_all_search_state const&) = delete;
  n_to_all_search_state& operator=(n_to_all_search_state const&) = delete;
  n_to_all_search_state(n_to_all_search_state&&) = default;
  n_to_all_search_state& operator=(n_to_all_search_state&&) = default;
  ~n_to_all_search_state() = default;

  std::vector<start> starts_;
  std::vector<std::vector<n_to_all_label>> results_;
};

struct n_to_all_search_stats {
  std::map<std::string, std::uint64_t> to_map() const {
    return {
        {"execute_time", execute_time_.count()},
    };
  }

  std::chrono::milliseconds execute_time_{0LL};
};

template <typename AlgoStats>
struct n_to_all_routing_result {
  std::vector<std::vector<n_to_all_label>> const* labels_{nullptr};
  interval<unixtime_t> interval_;
  n_to_all_search_stats search_stats_;
  AlgoStats algo_stats_;
};

template <direction SearchDir, typename Algo>
struct n_to_all_search {
  using algo_state_t = typename Algo::algo_state_t;
  using algo_stats_t = typename Algo::algo_stats_t;
  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);
};

}  // namespace nigiri::routing