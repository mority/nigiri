#include "../../../include/nigiri/routing/tripbased/tb.h"

namespace nigiri::routing::tripbased {

void transfer_set::emplace_back(const transfer& t) {
  transports_from_.emplace_back(t.transport_idx_from_);
  locations_from_.emplace_back(t.location_idx_from_);
  transports_to_.emplace_back(t.transport_idx_to_);
  locations_to_.emplace_back(t.location_idx_to_);
  bitfields_from_.emplace_back(t.bitfield_from_);
  shift_amounts_to_.emplace_back(t.shift_amount_to_);

  assert(transports_from_.size() == locations_from_.size());
  assert(transports_from_.size() == transports_to_.size());
  assert(transports_from_.size() == locations_to_.size());
  assert(transports_from_.size() == bitfields_from_.size());
  assert(transports_from_.size() == shift_amounts_to_.size());
}

void transfer_set::insert(const transfer_idx_t transfer_idx, const transfer& t) {
  transports_from_.insert(&transports_from_[transfer_idx], t.transport_idx_from_);
  locations_from_.insert(&locations_from_[transfer_idx], t.location_idx_from_);
  transports_to_.insert(&transports_to_[transfer_idx], t.transport_idx_to_);
  locations_to_.insert(&locations_to_[transfer_idx], t.location_idx_to_);
  bitfields_from_.insert(&bitfields_from_[transfer_idx], t.bitfield_from_);
}

void transfer_set::add_sorted(const transfer& t) {

//  auto transport_from_it = std::lower_bound(this->transports_from_.begin(), this->transports_from_.end(), t.transport_idx_from_);
//  transfer_idx_t transport_from_idx = static_cast<transfer_idx_t>(std::distance(this->transports_from_.begin(), transport_from_it));
//
//  if(transport_from_idx >= this->transports_from_.size()) {
//    this->add(t); // no transfer from this transport yet -> add to the end of set
//  } else {
//    // find index for insertion based on location_from
//    auto location_from_idx = std::lower_bound(this->locations_from_.)
//  }

  // find index for insertion
  transfer_idx_t insertion_idx = find_insertion_idx(t);


}

transfer_idx_t transfer_set::find_insertion_idx(const transfer& t) {
  transfer_idx_t left{0};
  transfer_idx_t right{this->transports_from_.size() - 1};
  while(left <= right) {
    transfer_idx_t mid{(left + right) / 2};
    if(this->transports_from_[mid] < t.transport_idx_from_) {
      left = mid + 1;
    } else if(this->transports_from_[mid] > t.transport_idx_from_) {
      right = mid - 1;
    } else {
      return mid;
    }
  }
  return this->transports_from_.empty() ? transfer_idx_t{0} : transfer_idx_t{this->transports_from_.size() - 1};
}

transfer transfer_set::get(transfer_idx_t const transfer_idx) {
  return transfer{transports_from_[transfer_idx],
                  locations_from_[transfer_idx],
                  transports_to_[transfer_idx],
                  locations_to_[transfer_idx],
                  bitfields_from_[transfer_idx],
                  shift_amounts_to_[transfer_idx]};
}

void transfer_set::sort() {}

void transfer_set::index() {}

} // namespace nigiri::routing::tripbased