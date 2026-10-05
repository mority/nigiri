#include "nigiri/routing/direct_filter.h"

#include <variant>

#include "utl/helpers/algorithm.h"

namespace nigiri::routing {

bool is_not_better_than_direct(query const& q, journey const& j) {
  if (q.direct_durations_.empty() || j.legs_.empty()) {
    return false;
  }

  auto const get_direct_duration = [&](transport_mode_t const m) {
    auto const it = utl::find_if(q.direct_durations_,
                                 [&](auto const& x) { return x.first == m; });
    return it == end(q.direct_durations_) ? duration_t::max() : it->second;
  };

  auto const* first = std::get_if<offset>(&j.legs_.front().uses_);
  auto const* last = std::get_if<offset>(&j.legs_.back().uses_);

  auto const longer_than_direct = [&](offset const* o) {
    return o != nullptr && o->duration() >= get_direct_duration(o->mode());
  };

  return longer_than_direct(first) || longer_than_direct(last) ||
         (first != nullptr && last != nullptr &&
          first->mode() == last->mode() &&
          first->duration() + last->duration() >=
              get_direct_duration(first->mode()));
}

}  // namespace nigiri::routing
