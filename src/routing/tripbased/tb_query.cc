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

// unsigned tb_query::r_query(transport_idx_t const transport_idx,
//                            bitfield const& bf) {
//   return 0U;
// }

// void tb_query::enqueue(const transport_idx_t transport_idx,
//                        const unsigned int stop_idx,
//                        const bitfield_idx_t& bf,
//                        const unsigned int n) {}
