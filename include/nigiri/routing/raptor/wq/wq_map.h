#pragma once

#include "nigiri/types.h"
#include <vector>

namespace nigiri::routing {

// reimplements
// https://github.com/kit-algo/ULTRA/blob/master/DataStructures/Container/Map.h
// for nigiri
struct wq_map {
  static constexpr size_t kNotContained = std::numeric_limits<size_t>::max();

  void resize(size_t new_size) {
    index_.resize(new_size, kNotContained);
    keys_.reserve(new_size);
    values_.reserve(new_size);
  }

  void clear() {
    utl::fill(index_, kNotContained);
    keys_.clear();
    values_.clear();
  }

  bool contains(route_idx_t const key) const {
    return index_[key.v_] != kNotContained;
  }

  bool insert(route_idx_t const key, stop_idx_t const value) {
    if (contains(key)) {
      values_[index_[key.v_]] = value;
      return false;
    } else {
      index_[key.v_] = keys_.size();
      keys_.emplace_back(key);
      values_.emplace_back(value);
      return true;
    }
  }

  stop_idx_t operator[](route_idx_t const key) const {
    return values_[index_[key.v_]];
  }

  std::vector<size_t> index_;
  std::vector<route_idx_t> keys_;
  std::vector<stop_idx_t> values_;
};

}  // namespace nigiri::routing