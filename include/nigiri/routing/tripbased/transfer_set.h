#pragma once

#include "nigiri/routing/tripbased/transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

struct preprocessing_stats {
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
       << "\n-----------------------\n";
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