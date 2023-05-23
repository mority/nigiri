#include "nigiri/routing/tripbased/tb_query_state.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query_state::reset() {
  l_.clear();
  r_.reset();
  q_.reset();
}