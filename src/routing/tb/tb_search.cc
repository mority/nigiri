#include "nigiri/routing/tb/tb_search.h"

namespace nigiri::routing::tb {

template <direction>
using trip_based = tb::query_engine<true>;

routing_result tb_search(timetable const& tt,
                         search_state& search_state,
                         query_state& algo_state,
                         query q) {
  return routing::search<direction::kForward, trip_based>{
      tt, nullptr, search_state, algo_state, std::move(q)}
      .execute();
}

}  // namespace nigiri::routing::tb