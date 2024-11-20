#include "nigiri/routing/query.h"

namespace nigiri::routing {

interval<unixtime_t> query::get_search_interval() const {
  return std::visit(
      utl::overloaded{[](interval<unixtime_t> const start_interval) {
                        return start_interval;
                      },
                      [](unixtime_t const start_time) {
                        return interval<unixtime_t>{start_time, start_time};
                      }},
      start_time_);
}

}  // namespace nigiri::routing