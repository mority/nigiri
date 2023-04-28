#include "nigiri/routing/tripbased/tb_query.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query::r_update(transport_idx_t const transport_idx,
                        unsigned const stop_idx,
                        bitfield const& bf) {
  bitfield bf_new = bf;

  // find first tuple of this transport_idx
  auto r_cur = std::lower_bound(
      r_.begin(), r_.end(), transport_idx,
      [](r_entry const& re, transport_idx_t const& tpi) constexpr {
        return re.transport_idx_ < tpi;
      });

  // no entry for this transport_idx
  if (r_cur == r_.end()) {
    r_.emplace_back(transport_idx, stop_idx, tbp_.get_or_create_bfi(bf_new));
    return;
  } else if (transport_idx < r_cur->transport_idx_) {
    r_.emplace(r_cur, transport_idx, stop_idx, tbp_.get_or_create_bfi(bf_new));
    return;
  }

  // remember first erasure as possible overwrite spot
  auto overwrite_spot = r_.end();

  while (r_cur != r_.end() && r_cur->transport_idx_ == transport_idx) {
    if (bf_new.none()) {
      // early termination
      return;
    } else if (stop_idx < r_cur->stop_idx_) {
      // new stop index is better than current
      bitfield bf_cur = tbp_.tt_.bitfields_[r_cur->bitfield_idx_] & ~bf_new;
      if (bf_cur.none()) {
        if (overwrite_spot == r_.end()) {
          overwrite_spot = r_cur;
          ++r_cur;
        } else {
          r_.erase(r_cur);
        }
      } else {
        r_cur->bitfield_idx_ = tbp_.get_or_create_bfi(bf_cur);
        ++r_cur;
      }
    } else if (stop_idx == r_cur->stop_idx_) {
      // new stop index is equal to current
      bf_new |= tbp_.tt_.bitfields_[r_cur->bitfield_idx_];
      if (overwrite_spot == r_.end()) {
        overwrite_spot = r_cur;
        ++r_cur;
      } else {
        r_.erase(r_cur);
      }
    } else {
      // new stop index is worse than current
      bf_new &= ~tbp_.tt_.bitfields_[r_cur->bitfield_idx_];
      ++r_cur;
    }
  }

  if (bf_new.any()) {
    if (overwrite_spot == r_.end()) {
      r_.emplace(r_cur, transport_idx, stop_idx,
                 tbp_.get_or_create_bfi(bf_new));
    } else {
      overwrite_spot->stop_idx_ = stop_idx;
      overwrite_spot->bitfield_idx_ = tbp_.get_or_create_bfi(bf_new);
    }
  }
}

unsigned tb_query::r_query(transport_idx_t const transport_idx,
                           bitfield const& bf_query) {

  // find first entry for this transport_idx
  auto r_cur = std::lower_bound(
      r_.begin(), r_.end(), transport_idx,
      [](r_entry const& re, transport_idx_t const& tpi) constexpr {
        return re.transport_idx_ < tpi;
      });

  // find matching entry for provided bitfield
  while (r_cur != r_.end() && transport_idx == r_cur->transport_idx_) {
    if ((tbp_.tt_.bitfields_[r_cur->bitfield_idx_] & bf_query).any()) {
      return r_cur->stop_idx_;
    }
    ++r_cur;
  }

  // no entry for this transport_idx
  // return stop index of final stop of the transport
  return tbp_.tt_.route_location_seq_[tbp_.tt_.transport_route_[transport_idx]]
             .size() -
         1;
}

void tb_query::enqueue(const transport_idx_t transport_idx,
                       const unsigned int stop_idx,
                       bitfield const& bf,
                       const unsigned int n) {
  auto const r_query_res = r_query(transport_idx, bf);
  if (stop_idx < r_query_res) {

    // new n?
    if (n == q_cur_.size()) {
      q_cur_.emplace_back(q_.size());
      q_start_.emplace_back(q_.size());
      q_end_.emplace_back(q_.size());
    }

    // add transport segment
    q_.emplace_back(transport_idx, stop_idx, r_query_res,
                    tbp_.get_or_create_bfi(bf));
    ++q_end_[n];

    // construct bf_new
    auto k{0U};
    for (; k < bf.size(); ++k) {
      if (bf.test(k)) {
        break;
      }
    }
    bitfield bf_new = ~bitfield{} << k;

    // update all transports of this route
    auto const route_idx = tbp_.tt_.transport_route_[transport_idx];
    for (auto const transport_idx_it :
         tbp_.tt_.route_transport_ranges_[route_idx]) {

      // set the bit of the day of the instance to false if the current
      // transport is earlier than the newly enqueued
      auto const mam_u =
          tbp_.tt_.event_mam(transport_idx_it, 0U, event_type::kDep);
      auto const mam_t =
          tbp_.tt_.event_mam(transport_idx, 0U, event_type::kDep);
      if (mam_u < mam_t) {
        bf_new.set(k, false);
      } else {
        bf_new.set(k, true);
      }

      r_update(transport_idx_it, stop_idx, bf_new);
    }
  }
}

void tb_query::reset() {
  r_.clear();
  q_cur_.clear();
  q_cur_.emplace_back(0U);
  q_start_.clear();
  q_start_.emplace_back(0U);
  q_end_.clear();
  q_end_.emplace_back(0U);
  l_.clear();
  j_.clear();
}

void tb_query::earliest_arrival_query(nigiri::routing::query query) { reset(); }
