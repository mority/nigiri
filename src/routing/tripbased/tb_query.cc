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
  }
}

unsigned tb_query::r_query(transport_idx_t const transport_idx,
                           bitfield const& bf) {
  return 0U;
}

void tb_query::enqueue(const transport_idx_t transport_idx,
                       const unsigned int stop_idx,
                       const bitfield_idx_t& bf,
                       const unsigned int n) {}
