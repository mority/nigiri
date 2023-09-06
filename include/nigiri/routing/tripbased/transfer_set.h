#pragma once

#include "nigiri/routing/tripbased/preprocessing/expanded_transfer.h"
#include "nigiri/routing/tripbased/transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

struct preprocessing_stats {
  std::string hh_mm_ss_str() const {
    std::uint32_t uint_time = std::round(time_.count());
    std::uint32_t hours = 0, minutes = 0;
    while (uint_time > 3600) {
      uint_time -= 3600;
      ++hours;
    }
    while (uint_time > 60) {
      uint_time -= 60;
      ++minutes;
    }
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << hours << ":" << std::setw(2)
       << std::setfill('0') << minutes << ":" << std::setw(2)
       << std::setfill('0') << uint_time;
    return ss.str();
  }

  void print(std::ostream& out) const {
    std::stringstream ss;
    ss << "--- Preprocessing Stats ---\nLine-based Pruning: "
       << (line_based_pruning ? "ON\n" : "OFF\n")
       << "U-turn Removal: " << (u_turn_removal ? "ON\n" : "OFF\n")
       << "Transfer Reduction: " << (transfer_reduction ? "ON\n" : "OFF\n")
       << "Initial Number of Transfers: " << n_transfers_initial_
       << "\nRemoved U-turn transfers: " << n_u_turn_transfers_
       << "\nRemoved by Transfer Reduction: " << n_transfers_reduced_
       << "\nFinal Number of Transfers: " << n_transfers_final_
       << "\nRequired Storage Space: "
       << static_cast<double>(n_transfers_final_ * sizeof(transfer)) / 1e9
       << " Gigabyte\nNew unique bitfields: " << n_new_bitfields_
       << "\nBitfield deduplication storage savings: "
       << static_cast<double>((n_transfers_final_ * sizeof(expanded_transfer)) -
                              (n_transfers_final_ * sizeof(transfer)) +
                              (n_new_bitfields_ * sizeof(bitfield))) /
              1e9
       << " Gigabyte\nPreprocessing time [hh:mm:ss]: " << hh_mm_ss_str()
       << "\nPeak memory usage: " << peak_memory_usage_ << " Gigabyte\n";
    out << ss.str();
  }

  // if line-based pruning was used
  bool line_based_pruning{false};
  // if U-turn removal was used
  bool u_turn_removal{false};
  // if transfer reduction was performed
  bool transfer_reduction{false};
  // number of transfers initially generated
  std::uint64_t n_transfers_initial_{0U};
  // number of transfers removed by U-turn removal
  std::uint64_t n_u_turn_transfers_{0U};
  // number of transfers removed by transfer reduction
  std::uint64_t n_transfers_reduced_{0U};
  // final number of transfers
  std::uint64_t n_transfers_final_{0U};
  // number of unique, new bitfields for transfers
  std::uint64_t n_new_bitfields_{0U};
  // time that war required to build this transfer set
  std::chrono::duration<double, std::ratio<1>> time_;
  // peak memory usage in Gigabyte
  double peak_memory_usage_ = 0;
};

struct transfer_set {
  auto at(std::uint32_t const transport_idx,
          std::uint32_t const stop_idx) const {
    return data_.at(transport_idx, stop_idx);
  }

  void write(cista::memory_holder&) const;
  void write(std::filesystem::path const&) const;
  static cista::wrapped<transfer_set> read(cista::memory_holder&&);

  std::size_t hash();

  // hash of the timetable for which the transfer set was built
  std::size_t tt_hash_{0U};
  // the number of elementary connections in the timetable
  std::uint64_t num_el_con_{0U};
  // length of the longest route in the timetable
  std::size_t route_max_length_{0U};
  // max. transfer time used during preprocessing
  std::uint16_t transfer_time_max_{0U};
  // the number of transfers found
  std::uint64_t n_transfers_{0U};
  // if building successfully finished
  bool ready_{false};
  // stats generated during preprocessing
  preprocessing_stats prepro_stats_;

  // the transfer set
  nvec<std::uint32_t, transfer, 2> data_;
};

}  // namespace nigiri::routing::tripbased