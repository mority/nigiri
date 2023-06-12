#pragma once

#include "nigiri/routing/tripbased/transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

constexpr auto const kMode =
    cista::mode::WITH_INTEGRITY | cista::mode::WITH_STATIC_VERSION;

struct transfer_set_stats {
  // the number of elementary connections in the timetable
  unsigned num_el_con_ = 0U;
  // length of the longest route
  std::size_t route_max_length_ = 0U;
  // max. look-ahead
  day_idx_t sigma_w_max_{};
  // the number of transfers found
  unsigned n_transfers_ = 0U;
  // true if the transfer set was successfully built or loaded
  bool ready_{false};
};

struct transfer_set {

  void write(cista::memory_holder&) const;
  void write(std::filesystem::path const&) const;
  static cista::wrapped<transfer_set> read(cista::memory_holder&&);

  transfer_set_stats stats_;

  // the transfer set
  nvec<std::uint32_t, transfer, 2> transfers_;
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