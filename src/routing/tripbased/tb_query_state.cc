#include "nigiri/routing/tripbased/tb_query_state.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query_state::reset() { q_.reset(); }

void tb_query_state::reset_arrivals() {
  reset();
  r_.reset();
  std::fill(t_min_.begin(), t_min_.end(), duration_t::max());
}