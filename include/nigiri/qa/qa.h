#pragma once

#include "cista/memory_holder.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"

#include <filesystem>

namespace nigiri::qa {

constexpr auto const kMaxRating = std::numeric_limits<double>::max();
constexpr auto const kMinRating = std::numeric_limits<double>::min();
using criteria_t = std::array<double, 3>;

struct query_results {
  std::uint64_t query_idx_;
  std::chrono::milliseconds response_time_;
  pareto_set<routing::journey> journeys_;
};

struct benchmark_results {
  void write(std::filesystem::path const&) const;
  static cista::wrapped<benchmark_results> read(cista::memory_holder&&);

  vector<query_results> results_;
};

double rate(vector<criteria_t> const&, vector<criteria_t> const&);

double rate(pareto_set<nigiri::routing::journey> const&,
            pareto_set<nigiri::routing::journey> const&);

}  // namespace nigiri::qa