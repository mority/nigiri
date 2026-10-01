#pragma once

#include <optional>
#include <variant>
#include <vector>

#include "utl/helpers/algorithm.h"
#include "utl/raii.h"

#include "nigiri/routing/journey.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

// Reference implementation for alternatives (expensive, akin to Yen):
// search, then block the routes and the transfer stations of the fastest
// journey one after another and search again.

enum class block_kind : std::uint8_t { kRoute, kTransfer };

struct alternative {
  journey j_;
  block_kind kind_;
  std::uint8_t idx_;  // transit leg / transfer of the reference journey
};

struct alternatives {
  std::vector<journey> base_;
  std::optional<journey> ref_;
  std::vector<alternative> alts_;
  unsigned n_searches_{0U};
};

inline bool is_transit(journey::leg const& l) {
  return std::holds_alternative<journey::run_enter_exit>(l.uses_);
}

template <typename Fn>
void for_each_station_location(timetable const& tt,
                               location_idx_t const l,
                               Fn&& fn) {
  auto const root = tt.locations_.get_root_idx(l);
  fn(root);
  for (auto const c : tt.locations_.children_[root]) {
    fn(c);
    for (auto const cc : tt.locations_.children_[c]) {
      fn(cc);
    }
  }
}

inline alternatives find_alternatives(timetable const& tt,
                                      rt_timetable const* rtt,
                                      search_state& ss,
                                      raptor_state& rs,
                                      query const& q,
                                      direction const dir) {
  auto a = alternatives{};

  auto const search = [&]() {
    ++a.n_searches_;
    auto const r = raptor_search(tt, rtt, ss, rs, q, dir);
    auto journeys = std::vector<journey>{};
    for (auto const& j : *r.journeys_) {
      if (!j.error_ && !j.legs_.empty()) {
        journeys.push_back(j);
      }
    }
    return journeys;
  };

  auto const unblock = utl::make_finally([&]() {
    rs.blocked_routes_.resize(0U);
    rs.blocked_locations_.resize(0U);
  });
  rs.blocked_routes_.resize(0U);
  rs.blocked_locations_.resize(0U);

  a.base_ = search();
  if (a.base_.empty()) {
    return a;
  }

  a.ref_ = *std::min_element(begin(a.base_), end(a.base_),
                             [](journey const& x, journey const& y) {
                               return std::pair{x.travel_time(), x.transfers_} <
                                      std::pair{y.travel_time(), y.transfers_};
                             });

  auto const add = [&](std::vector<journey>&& journeys, block_kind const kind,
                       std::uint8_t const idx) {
    for (auto& j : journeys) {
      auto const known = utl::any_of(a.base_,
                                     [&](journey const& x) {
                                       return x.legs_ == j.legs_;
                                     }) ||
                         utl::any_of(a.alts_, [&](alternative const& x) {
                           return x.j_.legs_ == j.legs_;
                         });
      if (!known) {
        a.alts_.push_back({std::move(j), kind, idx});
      }
    }
  };

  auto transit = std::vector<journey::leg const*>{};
  for (auto const& l : a.ref_->legs_) {
    if (is_transit(l)) {
      transit.push_back(&l);
    }
  }

  // Block routes.
  rs.blocked_routes_.resize(tt.n_routes());
  for (auto i = 0U; i != transit.size(); ++i) {
    auto const& run = std::get<journey::run_enter_exit>(transit[i]->uses_).r_;
    if (!run.is_scheduled()) {
      continue;
    }
    auto const r = tt.transport_route_[run.t_.t_idx_];
    rs.blocked_routes_.set(to_idx(r), true);
    add(search(), block_kind::kRoute, static_cast<std::uint8_t>(i));
    rs.blocked_routes_.set(to_idx(r), false);
  }
  rs.blocked_routes_.resize(0U);

  // Block transfer stations.
  rs.blocked_locations_.resize(tt.n_locations());
  for (auto i = 0U; i + 1U < transit.size(); ++i) {
    auto const set = [&](bool const b) {
      for (auto const l : {transit[i]->to_, transit[i + 1U]->from_}) {
        for_each_station_location(tt, l, [&](location_idx_t const x) {
          rs.blocked_locations_.set(to_idx(x), b);
        });
      }
    };
    set(true);
    add(search(), block_kind::kTransfer, static_cast<std::uint8_t>(i));
    set(false);
  }

  return a;
}

}  // namespace nigiri::routing
