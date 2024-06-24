#pragma once

#include "nigiri/types.h"
#include <vector>

namespace nigiri::routing {

// reimplements
// https://github.com/kit-algo/ULTRA/blob/master/DataStructures/Container/Set.h
// for nigiri
struct wq_set {
  static constexpr size_t kNotContained = std::numeric_limits<size_t>::max();
  using Iterator = typename std::vector<location_idx_t>::const_iterator;

  Iterator begin() const { return values_.begin(); }

  Iterator end() const { return values_.end(); }

  bool empty() const { return values_.empty(); }

  void resize(size_t new_size) {
    index_.resize(new_size, kNotContained);
    values_.reserve(new_size);
  }

  bool contains(location_idx_t const l) {
    return index_[l.v_] != kNotContained;
  }

  bool insert(location_idx_t const l) {
    if (contains(l)) {
      return false;
    }
    index_[l.v_] = values_.size();
    values_.emplace_back(l);
    return true;
  }

  void clear() {
    utl::fill(index_, kNotContained);
    values_.clear();
  }

  std::vector<size_t> index_;
  std::vector<location_idx_t> values_;
};

}  // namespace nigiri::routing