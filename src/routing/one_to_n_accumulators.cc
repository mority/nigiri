#include "nigiri/routing/one_to_n_accumulators.h"

#include <algorithm>
#include <iterator>
#include <ostream>

#include "fmt/format.h"

#include "date/date.h"

#include "utl/helpers/algorithm.h"
#include "utl/verify.h"

namespace nigiri::routing {

// ---------------------------------------------------------------------------
// raw writer
// ---------------------------------------------------------------------------

one_to_n_raw_writer::one_to_n_raw_writer(std::ostream& out, std::mutex& m)
    : out_{out}, mutex_{m} {}

std::unique_ptr<one_to_n_accumulator> one_to_n_raw_writer::clone() const {
  return std::make_unique<one_to_n_raw_writer>(out_, mutex_);
}

void one_to_n_raw_writer::begin_origin(cell_idx_t const o) {
  origin_ = o;
  buf_.clear();
}

void one_to_n_raw_writer::add_sample(unixtime_t const departure,
                                     one_to_n_sample const& s) {
  auto const dep = date::format("%FT%RZ", departure);
  auto out = std::back_inserter(buf_);
  for (auto const c : s.reached()) {
    for (auto const& l : s[c]) {
      fmt::format_to(out, "{},{},{},{},{}\n", to_idx(origin_), to_idx(c), dep,
                     l.k_, l.travel_time_.count());
    }
  }
}

void one_to_n_raw_writer::end_origin(cell_idx_t) {
  auto const lock = std::lock_guard{mutex_};
  out_ << buf_;
  buf_.clear();
}

// ---------------------------------------------------------------------------
// histogram
// ---------------------------------------------------------------------------

std::string one_to_n_histogram::header(config const& c) {
  auto h = std::string{"origin,destination,k,n_samples,unreached"};
  for (auto i = 0U; i != c.n_bins_; ++i) {
    fmt::format_to(std::back_inserter(h), ",bin_{}", i);
  }
  h += '\n';
  return h;
}

one_to_n_histogram::one_to_n_histogram(config const& c,
                                       std::ostream& out,
                                       std::mutex& m)
    : config_{c}, out_{out}, mutex_{m} {
  utl::verify(c.n_bins_ != 0U && c.bin_width_ > duration_t{0} &&
                  c.max_k_ != std::numeric_limits<std::uint8_t>::max(),
              "one_to_n_histogram: need n_bins > 0, bin_width > 0, max_k < "
              "255");
}

std::unique_ptr<one_to_n_accumulator> one_to_n_histogram::clone() const {
  return std::make_unique<one_to_n_histogram>(config_, out_, mutex_);
}

void one_to_n_histogram::begin_origin(cell_idx_t const o) {
  if (counts_.empty()) {
    counts_.resize(static_cast<std::size_t>(config_.n_cells_) *
                   (config_.max_k_ + 1U) * config_.n_bins_);
    touched_.resize(config_.n_cells_);
  }
  origin_ = o;
  n_samples_ = 0U;
}

void one_to_n_histogram::add_sample(unixtime_t, one_to_n_sample const& s) {
  ++n_samples_;
  auto const width = config_.bin_width_.count();
  for (auto const c : s.reached()) {
    utl::verify(to_idx(c) < config_.n_cells_,
                "one_to_n_histogram: cell {} >= n_cells {}", to_idx(c),
                config_.n_cells_);
    if (!touched_.test(to_idx(c))) {
      touched_.set(to_idx(c), true);
      touched_list_.push_back(c);
    }

    // The labels are sorted by ascending k with decreasing travel time:
    // the best label with at most k trips is the last one with k_ <= k.
    auto const labels = s[c];
    auto j = 0U;
    for (auto k = std::uint8_t{0U}; k <= config_.max_k_; ++k) {
      while (j != labels.size() && labels[j].k_ <= k) {
        ++j;
      }
      if (j == 0U) {
        continue;
      }
      auto const bin =
          std::min(static_cast<std::uint32_t>(
                       labels[j - 1U].travel_time_.count() / width),
                   config_.n_bins_ - 1U);
      ++counts_[row(c, k) + bin];
    }
  }
}

void one_to_n_histogram::end_origin(cell_idx_t) {
  utl::sort(touched_list_);

  buf_.clear();
  auto out = std::back_inserter(buf_);
  for (auto const c : touched_list_) {
    for (auto k = std::uint8_t{0U}; k <= config_.max_k_; ++k) {
      auto const bins = std::span{counts_}.subspan(row(c, k), config_.n_bins_);
      auto reached = 0U;
      for (auto const n : bins) {
        reached += n;
      }
      if (reached == 0U) {
        continue;
      }
      fmt::format_to(out, "{},{},{},{},{}", to_idx(origin_), to_idx(c), k,
                     n_samples_, n_samples_ - reached);
      for (auto const n : bins) {
        fmt::format_to(out, ",{}", n);
      }
      buf_ += '\n';
    }
    std::fill_n(begin(counts_) + static_cast<std::ptrdiff_t>(row(c, 0U)),
                (config_.max_k_ + 1U) * config_.n_bins_, 0U);
    touched_.set(to_idx(c), false);
  }
  touched_list_.clear();

  auto const lock = std::lock_guard{mutex_};
  out_ << buf_;
}

}  // namespace nigiri::routing
