#include "nigiri/routing/tripbased/transfer_set.h"
#include "nigiri/logging.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void transfer_set::write(cista::memory_holder& mem) const {
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

void transfer_set::write(std::filesystem::path const& p) const {
  auto mmap = cista::mmap{p.string().c_str(), cista::mmap::protection::WRITE};
  auto writer = cista::buf<cista::mmap>(std::move(mmap));

  {
    auto const timer = scoped_timer{"transfer_set.write"};
    cista::serialize<kMode>(writer, *this);
  }
}

cista::wrapped<transfer_set> transfer_set::read(cista::memory_holder&& mem) {
  return std::visit(
      utl::overloaded{
          [&](cista::buf<cista::mmap>& b) {
            auto const ptr =
                reinterpret_cast<transfer_set*>(&b[cista::data_start(kMode)]);
            return cista::wrapped{std::move(mem), ptr};
          },
          [&](cista::buffer& b) {
            auto const ptr = cista::deserialize<transfer_set, kMode>(b);
            return cista::wrapped{std::move(mem), ptr};
          },
          [&](cista::byte_buf& b) {
            auto const ptr = cista::deserialize<transfer_set, kMode>(b);
            return cista::wrapped{std::move(mem), ptr};
          }},
      mem);
}
