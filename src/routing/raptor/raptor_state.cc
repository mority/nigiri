#include "nigiri/routing/raptor/raptor_state.h"

#include <algorithm>

#include "fmt/core.h"

#include "utl/helpers/algorithm.h"

#include "nigiri/routing/limits.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

raptor_state& raptor_state::resize(unsigned const n_locations,
                                   unsigned const n_routes,
                                   unsigned const n_rt_transports,
                                   via_offset_t const n_vias) {
  // Only grows: a state is reused for queries with different via counts, and
  // shrinking would not free memory but zero it again on the next growth.
  auto const grow = [](std::vector<delta_t>& v, std::size_t const size) {
    if (v.size() < size) {
      v.resize(size);
    }
  };

  auto const n_entries = std::size_t{n_locations} * (n_vias + 1U);
  n_locations_ = n_locations;
  grow(tmp_storage_, n_entries);
  grow(best_storage_, n_entries);
  grow(round_times_storage_, n_entries * (kMaxTransfers + 2U));
  grow(bounds_storage_, n_entries * (kMaxTransfers + 2U));
  station_mark_.resize(n_locations);
  prev_station_mark_.resize(n_locations);
  route_mark_.resize(n_routes);
  rt_transport_mark_.resize(n_rt_transports);
  return *this;
}

template <via_offset_t Vias>
void raptor_state::print(timetable const& tt,
                         date::sys_days const base,
                         delta_t const invalid) {
  auto invalid_array = std::array<delta_t, Vias + 1>{};
  invalid_array.fill(invalid);

  auto const& tmp = get_tmp<Vias>();
  auto const& best = get_best<Vias>();
  auto const& round_times = get_round_times<Vias>();

  auto const has_empty_rounds = [&](std::uint32_t const l) {
    for (auto k = 0U; k != kMaxTransfers + 2U; ++k) {
      if (round_times[k][l] != invalid_array) {
        return false;
      }
    }
    return true;
  };

  auto const print_delta = [&](delta_t const d) {
    if (d == invalid) {
      fmt::print("________________");
    } else {
      fmt::print("{:16}", delta_to_unix(base, d));
    }
  };

  auto const print_deltas = [&](std::array<delta_t, Vias + 1> const& deltas) {
    fmt::print("[ ");
    for (auto const d : deltas) {
      print_delta(d);
      fmt::print(" ");
    }
    fmt::print("]");
  };

  for (auto l = 0U; l != tt.n_locations(); ++l) {
    if (best[l] == invalid_array && has_empty_rounds(l)) {
      continue;
    }

    fmt::print("{:80}  ", fmt::streamed(loc{tt, location_idx_t{l}}));

    fmt::print("tmp=");
    print_deltas(tmp[l]);
    fmt::print(", ");

    auto const& b = best[l];
    fmt::print("best=");
    print_deltas(b);
    fmt::print(", round_times: ");
    for (auto i = 0U; i != kMaxTransfers + 2U; ++i) {
      auto const& t = round_times[i][l];
      fmt::print("{}:", i);
      print_deltas(t);
      fmt::print(" ");
    }
    fmt::print("\n");
  }
}

static_assert(kMaxVias == 2,
              "raptor_state.cc needs to be adjusted for kMaxVias");

template void raptor_state::print<0>(timetable const&, date::sys_days, delta_t);
template void raptor_state::print<1>(timetable const&, date::sys_days, delta_t);
template void raptor_state::print<2>(timetable const&, date::sys_days, delta_t);

}  // namespace nigiri::routing
