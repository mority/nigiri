#include "nigiri/qa/qa.h"

#include "boost/program_options.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
namespace bpo = boost::program_options;
using namespace nigiri::qa;

// needs sorted vector
template <typename T>
T quantile(std::vector<T> const& v, double q) {
  q = q < 0.0 ? 0.0 : q;
  q = 1.0 < q ? 1.0 : q;
  if (q == 1.0) {
    return v.back();
  }
  return v[static_cast<std::size_t>(v.size() * q)];
}

void print_result(
    std::vector<std::pair<double, std::chrono::milliseconds>> const& var,
    std::string const& var_name) {
  auto const pair_str = [](auto const& p) {
    auto ss = std::stringstream{};
    ss << "(" << p.first << ", " << p.second << ")";
    return ss.str();
  };

  std::cout << "\n--- " << var_name << " --- (n = " << var.size() << ")"
            << "\n  10%: " << pair_str(quantile(var, 0.1))
            << "\n  20%: " << pair_str(quantile(var, 0.2))
            << "\n  30%: " << pair_str(quantile(var, 0.3))
            << "\n  40%: " << pair_str(quantile(var, 0.4))
            << "\n  50%: " << pair_str(quantile(var, 0.5))
            << "\n  60%: " << pair_str(quantile(var, 0.6))
            << "\n  70%: " << pair_str(quantile(var, 0.7))
            << "\n  80%: " << pair_str(quantile(var, 0.8))
            << "\n  90%: " << pair_str(quantile(var, 0.9))
            << "\n  99%: " << pair_str(quantile(var, 0.99))
            << "\n99.9%: " << pair_str(quantile(var, 0.999))
            << "\n  max: " << pair_str(var.back())
            << "\n----------------------------------\n";
}

void compare_heuristic(benchmark_results const& ref_file,
                       benchmark_results const& cmp_file) {
  auto rating_timing =
      std::vector<std::pair<double, std::chrono::milliseconds>>{};
  rating_timing.reserve(ref_file.results_.size());
  for (auto const& ref : ref_file.results_) {
    for (auto const& cmp : cmp_file.results_) {
      if (ref.query_idx_ == cmp.query_idx_) {
        auto const rating = nigiri::qa::rate(cmp.journeys_, ref.journeys_);
        auto const timing = cmp.response_time_ - ref.response_time_;
        rating_timing.emplace_back(rating, timing);
        break;
      }
    }
  }

  std::sort(begin(rating_timing), end(rating_timing),
            [](auto const& a, auto const& b) { return a.first < b.first; });
  print_result(rating_timing, "rating");

  std::sort(begin(rating_timing), end(rating_timing),
            [](auto const& a, auto const& b) { return a.second < b.second; });
  print_result(rating_timing, "timing");
}

void compare_algorithmic(benchmark_results const& ref_file,
                         benchmark_results const& cmp_file) {
  auto empty = 0U;
  auto mismatches = 0U;
  auto additional_journeys = 0U;
  auto missing_journeys = 0U;

  for (auto const& ref : ref_file.results_) {
    if (ref.journeys_.size() == 0) {
      ++empty;
    }
    if (ref.)
      for (auto const& cmp : cmp_file.results_) {
      }
  }
}

int main(int ac, char** av) {
  auto ref_path = fs::path{};
  auto cmp_path = fs::path{};

  enum mode { kAlgorithmic, kHeuristic };
  auto const mode_map = std::map<std::string, mode>{
      {"algorithmic", mode::kAlgorithmic}, {"heuristic", mode::kHeuristic}};
  auto mode_str = std::string{};

  auto desc = bpo::options_description{"Options"};
  desc.add_options()  //
      ("help,h", "produce this help message")  //
      ("reference,r", bpo::value(&ref_path)->required(),
       "path to binary dump of vector<pareto_set<journey>>")  //
      ("compare,c", bpo::value(&cmp_path)->required(),
       "path to binary dump of vector<pareto_set<journey>>")  //
      ("mode,m", bpo::value(&mode_str)->default_value("algorithmic"),  //
       "comparison mode: algorithmic | heuristic");
  auto const pos = bpo::positional_options_description{}.add("in", -1);

  auto vm = bpo::variables_map{};
  bpo::store(
      bpo::command_line_parser(ac, av).options(desc).positional(pos).run(), vm);
  bpo::notify(vm);

  if (vm.count("help") != 0U) {
    std::cout << desc << "\n";
    return 0;
  }

  if (vm.count("reference") != 1) {
    std::cout << "Error: please provide exactly one reference file\n";
    return 1;
  }

  if (vm.count("compare") != 1) {
    std::cout << "Error: please provide exactly one compare file\n";
    return 1;
  }

  auto m = mode::kAlgorithmic;
  if (mode_map.contains(mode_str)) {
    m = mode_map.at(mode_str);
  } else {
    std::cout << "Error: unknown mode string\n";
    return 1;
  }

  auto const ref_file = nigiri::qa::benchmark_results::read(
      cista::memory_holder{cista::file{ref_path.c_str(), "r"}.content()});
  auto const cmp_file = nigiri::qa::benchmark_results::read(
      cista::memory_holder{cista::file{cmp_path.c_str(), "r"}.content()});

  switch (m) {
    case kHeuristic: compare_heuristic(*ref_file, *cmp_file); break;
    case kAlgorithmic: compare_algorithmic(*ref_file, *cmp_file); break;
  }

  return 0;
}