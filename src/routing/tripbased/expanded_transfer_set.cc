#include "nigiri/routing/tripbased/expanded_transfer_set.h"
#include "boost/functional/hash.hpp"
#include "nigiri/logging.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

constexpr auto const kMode =
    cista::mode::WITH_INTEGRITY | cista::mode::WITH_STATIC_VERSION;

void expanded_transfer_set::write(cista::memory_holder& mem) const {
  std::visit(utl::overloaded{[&](cista::buf<cista::mmap>& writer) {
                               cista::serialize<kMode>(writer, *this);
                             },
                             [&](cista::buffer&) {
                               throw std::runtime_error{"not supported"};
                             },
                             [&](cista::byte_buf& b) {
                               auto writer = cista::buf{std::move(b)};
                               cista::serialize<kMode>(writer, *this);
                               b = std::move(writer.buf_);
                             }},
             mem);
}

void expanded_transfer_set::write(std::filesystem::path const& p) const {
  auto mmap = cista::mmap{p.string().c_str(), cista::mmap::protection::WRITE};
  auto writer = cista::buf<cista::mmap>(std::move(mmap));

  {
    auto const timer = scoped_timer{"expanded_transfer_set.write"};
    cista::serialize<kMode>(writer, *this);
  }
}

cista::wrapped<expanded_transfer_set> expanded_transfer_set::read(
    cista::memory_holder&& mem) {
  return std::visit(
      utl::overloaded{[&](cista::buf<cista::mmap>& b) {
                        auto const ptr =
                            reinterpret_cast<expanded_transfer_set*>(
                                &b[cista::data_start(kMode)]);
                        return cista::wrapped{std::move(mem), ptr};
                      },
                      [&](cista::buffer& b) {
                        auto const ptr =
                            cista::deserialize<expanded_transfer_set, kMode>(b);
                        return cista::wrapped{std::move(mem), ptr};
                      },
                      [&](cista::byte_buf& b) {
                        auto const ptr =
                            cista::deserialize<expanded_transfer_set, kMode>(b);
                        return cista::wrapped{std::move(mem), ptr};
                      }},
      mem);
}

std::size_t expanded_transfer_set::hash() {
  std::size_t seed = 0;

  boost::hash_combine(seed, data_.size());
  for (std::uint32_t t = 0; t != data_.size(); ++t) {
    boost::hash_combine(seed, data_.size(t));
    for (std::uint32_t l = 0; l != data_.size(t); ++l) {
      boost::hash_combine(seed, data_.size(t, l));
      for (auto const& transfer : data_.at(t, l)) {
        boost::hash_combine(seed, transfer.transport_idx_to_.v_);
        boost::hash_combine(seed, transfer.stop_idx_to_);
        boost::hash_combine(seed, transfer.passes_midnight_);
      }
    }
  }

  return seed;
}