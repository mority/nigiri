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

template <std::size_t NMaxTypes>
constexpr auto static_type_hash(transfer_set const*,
                                cista::hash_data<NMaxTypes> h) noexcept {
  return h.combine(cista::hash("nigiri::routing::tripbased::transfer_set"));
}

template <typename Ctx>
inline void serialize(Ctx&, transfer_set const*, cista::offset_t const) {}

template <typename Ctx>
inline void deserialize(Ctx const&, transfer_set*) {}

}  // namespace nigiri::routing::tripbased