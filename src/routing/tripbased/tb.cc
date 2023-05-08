#include "../../../include/nigiri/routing/tripbased/tb.h"

namespace nigiri::routing::tripbased {

void hash_transfer_set::add(transport_idx_t const& transport_idx_from,
                            unsigned const stop_idx_from,
                            transport_idx_t const& transport_idx_to,
                            unsigned const stop_idx_to,
                            bitfield_idx_t const& bitfield_idx_from,
                            int shift_amount) {
  assert(!finalized_);

  if (!initialized_) {
    cur_transport_idx_from_ = transport_idx_from;
    cur_stop_idx_from_ = stop_idx_from;
    initialized_ = true;
  }

  if (stop_idx_from != cur_stop_idx_from_ ||
      transport_idx_from != cur_transport_idx_from_) {
    // finish current stop
    key key{};
    key.first = cur_transport_idx_from_;
    key.second = cur_stop_idx_from_;
    entry entry{};
    entry.first = cur_start_;
    entry.second = cur_start_ + cur_length_;
    index_.emplace(key, entry);

    // reset cursors
    cur_transport_idx_from_ = transport_idx_from;
    cur_stop_idx_from_ = stop_idx_from;
    cur_start_ += cur_length_;
    cur_length_ = 0U;
  }

  transfers_.emplace_back(transport_idx_to, stop_idx_to, bitfield_idx_from,
                          shift_amount);
  ++cur_length_;
}

void hash_transfer_set::finalize() {
  key key{cur_transport_idx_from_, cur_stop_idx_from_};
  entry entry{cur_start_, cur_start_ + cur_length_};
  index_.emplace(key, entry);
  finalized_ = true;
}

std::optional<hash_transfer_set::entry> hash_transfer_set::get_transfers(
    transport_idx_t const& transport_idx_from, unsigned const stop_idx_from) {
  assert(finalized_);
  key key{transport_idx_from, stop_idx_from};
  return index_.contains(key)
             ? std::make_optional<hash_transfer_set::entry>(index_.at(key))
             : std::nullopt;
}

}  // namespace nigiri::routing::tripbased