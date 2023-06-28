#pragma once

#include "nigiri/routing/tripbased/expanded_transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

struct expanded_transfer_set {

  void write(cista::memory_holder&) const;
  void write(std::filesystem::path const&) const;
  static cista::wrapped<expanded_transfer_set> read(cista::memory_holder&&);

  std::size_t hash();

  // the transfer set
  nvec<std::uint32_t, expanded_transfer, 2> data_;
};

}  // namespace nigiri::routing::tripbased