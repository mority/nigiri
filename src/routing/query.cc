#include "nigiri/routing/query.h"

#include "nigiri/for_each_meta.h"

namespace nigiri::routing {

bool query::operator==(query const& o) const {
  auto const eq = [](auto&& a, auto&& b) {
    using T = std::decay_t<decltype(a)>;
    if constexpr (std::is_floating_point_v<T>) {
      return std::abs(a - b) < 0.0001;
    } else {
      return a == b;
    }
  };
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return (eq(std::get<I>(cista::to_tuple(o)),
               std::get<I>(cista::to_tuple(*this))) &&
            ...);
  }(std::make_index_sequence<
             std::tuple_size_v<decltype(cista::to_tuple(*this))>>());
}

void sanitize_query(query& q) {
  if (q.max_travel_time_.count() < 0 || q.max_travel_time_ > kMaxTravelTime) {
    q.max_travel_time_ = kMaxTravelTime;
  }
}

void sanitize_via_stops(timetable const& tt, query& q) {
  while (q.via_stops_.size() >= 2) {
    auto updated = false;
    for (auto i = 0U; i < q.via_stops_.size() - 1; ++i) {
      auto& a = q.via_stops_[i];
      auto& b = q.via_stops_[i + 1];
      if (matches(tt, location_match_mode::kEquivalent, a.location_,
                  b.location_)) {
        a.stay_ += b.stay_;
        q.via_stops_.erase(q.via_stops_.begin() + i + 1);
        updated = true;
        break;
      }
    }
    if (!updated) {
      break;
    }
  }
}

void query::sanitize(timetable const& tt) {
  utl::verify(via_stops_.size() <= kMaxVias,
              "too many via stops: {}, limit: {}", via_stops_.size(), kMaxVias);

  sanitize_query(*this);
  sanitize_via_stops(tt, *this);
}

}  // namespace nigiri::routing