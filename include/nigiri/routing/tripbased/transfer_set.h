#pragma once

#include "nigiri/routing/tripbased/transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

constexpr auto const kMode =
    cista::mode::WITH_INTEGRITY | cista::mode::WITH_STATIC_VERSION;

struct transfer_set {
  auto at(std::uint32_t const transport_idx,
          std::uint32_t const location_idx) const {
    return data_.at(transport_idx, location_idx);
  }

  void write(cista::memory_holder&) const;
  void write(std::filesystem::path const&) const;
  static cista::wrapped<transfer_set> read(cista::memory_holder&&);

  std::size_t hash();

  // hash of the timetable for which the transfer set was built
  std::size_t tt_hash_ = 0U;
  // the number of elementary connections in the timetable
  unsigned num_el_con_ = 0U;
  // length of the longest route in the timetable
  std::size_t route_max_length_ = 0U;
  // max. transfer time used during preprocessing
  duration_t transfer_time_max_{0U};
  // the number of transfers found
  unsigned n_transfers_ = 0U;
  // if building successfully finished
  bool ready_{false};

  // the transfer set
  nvec<std::uint32_t, transfer, 2> data_;
};

}  // namespace nigiri::routing::tripbased