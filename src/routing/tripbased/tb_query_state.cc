#include "nigiri/routing/tripbased/tb_query_state.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void tb_query_state::reset() {
  l_.clear();
  // TODO reset r_
  q_.reset();
}