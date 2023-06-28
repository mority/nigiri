#pragma once

#include "nigiri/routing/tripbased/expanded_transfer.h"
#include <filesystem>
#include "cista/memory_holder.h"

namespace nigiri::routing::tripbased {

constexpr auto const kMode =
    cista::mode::WITH_INTEGRITY | cista::mode::WITH_STATIC_VERSION;

struct expanded_transfer_set {

  void write(cista::memory_holder&) const;
  void write(std::filesystem::path const&) const;
  static cista::wrapped<expanded_transfer_set> read(cista::memory_holder&&);

  std::size_t hash();

  // the transfer set
  std::vector<std::vector<std::vector<expanded_transfer>>> data_;
};

}  // namespace nigiri::routing::tripbased