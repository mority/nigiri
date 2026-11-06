#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nigiri/routing/limits.h"
#include "nigiri/routing/one_to_n.h"

namespace nigiri::routing {

// Writes every label of every sample as CSV:
//   origin,destination,departure,k,travel_time
// k = number of trips, travel_time in minutes. Lines of one origin are
// written together; the order of the origins is unspecified.
struct one_to_n_raw_writer final : public one_to_n_accumulator {
  static constexpr auto const kHeader =
      "origin,destination,departure,k,travel_time\n";

  one_to_n_raw_writer(std::ostream&, std::mutex&);

  std::unique_ptr<one_to_n_accumulator> clone() const override;
  void begin_origin(cell_idx_t) override;
  void add_sample(unixtime_t, one_to_n_sample const&) override;
  void end_origin(cell_idx_t) override;

private:
  std::ostream& out_;
  std::mutex& mutex_;
  cell_idx_t origin_{0U};
  std::string buf_;
};

// For each (origin, destination, k): histogram over the samples of the
// travel time with at most k trips, k = 0..max_k. Bin i counts travel times
// in [i * bin_width, (i + 1) * bin_width); the last bin also counts all longer
// ones. Written as CSV, one line per (origin, destination, k) that was reached
// in at least one sample:
//   origin,destination,k,n_samples,unreached,bin_0,...,bin_{n-1}
struct one_to_n_histogram final : public one_to_n_accumulator {
  struct config {
    std::uint32_t n_cells_{0U};
    duration_t bin_width_{5};
    std::uint32_t n_bins_{18U};
    std::uint8_t max_k_{kMaxTransfers + 1U};
  };

  static std::string header(config const&);

  one_to_n_histogram(config const&, std::ostream&, std::mutex&);

  std::unique_ptr<one_to_n_accumulator> clone() const override;
  void begin_origin(cell_idx_t) override;
  void add_sample(unixtime_t, one_to_n_sample const&) override;
  void end_origin(cell_idx_t) override;

private:
  std::size_t row(cell_idx_t const c, std::uint8_t const k) const {
    return (to_idx(c) * (config_.max_k_ + 1U) + k) * config_.n_bins_;
  }

  config config_;
  std::ostream& out_;
  std::mutex& mutex_;
  cell_idx_t origin_{0U};
  std::uint32_t n_samples_{0U};
  std::vector<std::uint32_t> counts_;  // [cell][k][bin]
  bitvec touched_;
  std::vector<cell_idx_t> touched_list_;
  std::string buf_;
};

}  // namespace nigiri::routing
