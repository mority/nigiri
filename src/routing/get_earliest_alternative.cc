#include "nigiri/routing/get_earliest_alternative.h"

#include <algorithm>

#include "utl/overloaded.h"
#include "utl/sorted_diff.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/get_earliest_transport.h"
#include "nigiri/routing/query.h"
#include "nigiri/rt/frun.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/td_footpath.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

template <direction SearchDir>
std::optional<std::array<journey::leg, 3U>> get_earliest_alternative(
    timetable const& tt,
    rt_timetable const* rtt,
    query const& q,
    location_idx_t const from,
    location_idx_t const to,
    unixtime_t const from_time,
    unixtime_t const to_time,
    bitvec& is_src,
    bitvec& is_dst,
    alternative_options const opt) {
  constexpr auto const kFwd = SearchDir == direction::kForward;

  // Event the transport is entered / exited at (in search direction).
  constexpr auto const kEnterEv = kFwd ? event_type::kDep : event_type::kArr;
  constexpr auto const kExitEv = kFwd ? event_type::kArr : event_type::kDep;

  // Time advances forward (kFwd) / backward (kBwd) in search direction.
  auto const adv = [](duration_t const d) { return kFwd ? d : -d; };
  auto const is_better = [](unixtime_t const a, unixtime_t const b) {
    return kFwd ? a < b : a > b;
  };
  auto const is_better_or_eq = [](unixtime_t const a, unixtime_t const b) {
    return kFwd ? a <= b : a >= b;
  };

  // Append only - a terminal expands into its whole station complex, so the
  // same route shows up many times; sorting and deduplicating once at the end
  // is much cheaper than merging into a sorted vector per location.
  auto const add_routes = [&](auto& dst, auto& marker, auto& copy_from,
                              location_idx_t const l) {
    marker.set(to_idx(l), true);
    dst.insert(end(dst), begin(copy_from[l]), end(copy_from[l]));
  };

  auto const add = [&](auto& dst, auto& marker, auto& copy_from,
                       location_idx_t const l, direction const dir) {
    add_routes(dst, marker, copy_from, l);
    for (auto const& fp : (dir == direction::kForward
                               ? tt.locations_.footpaths_out_
                               : tt.locations_.footpaths_in_)[q.prf_idx_][l]) {
      add_routes(dst, marker, copy_from, fp.target());
    }
  };

  auto const sort_unique = [](auto& v) {
    std::sort(begin(v), end(v));
    v.erase(std::unique(begin(v), end(v)), end(v));
  };

  is_src.resize(tt.n_locations());
  is_dst.resize(tt.n_locations());
  is_src.zero_out();
  is_dst.zero_out();

  // Determine earliest departure + adjusted transfers at ingress stops.
  auto ingress = hash_map<location_idx_t, std::pair<footpath, unixtime_t>>{};
  {
    // `emplace` keeps the first entry, so the zero-cost terminal expansion
    // below wins over a footpath to the same stop.
    auto const add_ingress = [&](location_idx_t const l, duration_t const d) {
      ingress.emplace(l, std::pair{footpath{l, d}, from_time + adv(d)});
    };

    if (opt.from_is_terminal_) {
      for_each_meta(tt, q.start_match_mode_, from,
                    [&](location_idx_t const m) { add_ingress(m, duration_t{0}); });
    } else {
      add_ingress(from, duration_t{adjusted_transfer_time(
                            q.transfer_time_settings_,
                            tt.locations_.transfer_time_[from])});
    }

    for (auto const fp : (kFwd ? tt.locations_.footpaths_out_
                               : tt.locations_.footpaths_in_)[q.prf_idx_][from]) {
      add_ingress(fp.target(), adjusted_transfer_time(q.transfer_time_settings_,
                                                      fp.duration()));
    }
  }

  // A terminal stands for its whole station complex, so every equivalent
  // location contributes its routes as well - routes only, mirroring
  // `get_starts`, which expands metas but does not chase their footpaths.
  // Recursing into each platform's footpaths instead would pull in every
  // station within walking distance of any platform.
  auto const add_all = [&](auto& dst, auto& marker, auto& copy_from,
                           location_idx_t const l, direction const dir,
                           bool const is_terminal,
                           location_match_mode const match_mode) {
    add(dst, marker, copy_from, l, dir);
    if (is_terminal) {
      for_each_meta(tt, match_mode, l, [&](location_idx_t const m) {
        add_routes(dst, marker, copy_from, m);
      });
    }
  };

  // Join all relevent routes.
  auto from_routes = std::vector<route_idx_t>{},
       to_routes = std::vector<route_idx_t>{};
  add_all(from_routes, is_src, tt.location_routes_, from, SearchDir,
          opt.from_is_terminal_, q.start_match_mode_);
  add_all(to_routes, is_dst, tt.location_routes_, to, flip(SearchDir),
          opt.to_is_terminal_, q.dest_match_mode_);
  sort_unique(from_routes);
  sort_unique(to_routes);

  // Join all relevent rt transports.
  auto from_rt_transports = std::vector<rt_transport_idx_t>{},
       to_rt_transports = std::vector<rt_transport_idx_t>{};
  if (rtt != nullptr) {
    add_all(from_rt_transports, is_src, rtt->location_rt_transports_, from,
            SearchDir, opt.from_is_terminal_, q.start_match_mode_);
    add_all(to_rt_transports, is_dst, rtt->location_rt_transports_, to,
            flip(SearchDir), opt.to_is_terminal_, q.dest_match_mode_);
    sort_unique(from_rt_transports);
    sort_unique(to_rt_transports);
  }

  // Visit route in RAPTOR without updating intermediate stops that are not the
  // destination  because there's no next round (no dynamic programming).
  auto const get_earliest =
      [&]<typename T>(T const x,  // route_idx_t | rt_transport_idx_t
                      stop_idx_t const stop_idx,
                      unixtime_t const time) -> std::optional<rt::frun> {
    if constexpr (std::is_same_v<T, rt_transport_idx_t>) {
      auto const ev = rtt->unix_event_time(x, stop_idx, kEnterEv);
      return is_better_or_eq(time, ev) ? std::optional{rt::frun::from_rt(tt, rtt, x)}
                                       : std::nullopt;
    } else {
      auto const [day, mam] = tt.day_idx_mam(time);
      if (rtt == nullptr) {
        auto const t = get_earliest_transport<SearchDir>(
            tt, tt, 0U, x, stop_idx, day, mam, location_idx_t::invalid(),
            [](day_idx_t, std::int16_t) { return false; });
        return t.is_valid() ? std::optional{rt::frun::from_t(tt, rtt, t)}
                            : std::nullopt;
      } else {
        auto const t = get_earliest_transport<SearchDir>(
            tt, *rtt, 0U, x, stop_idx, day, mam, location_idx_t::invalid(),
            [](day_idx_t, std::int16_t) { return false; });
        return t.is_valid() ? std::optional{rt::frun::from_t(tt, rtt, t)}
                            : std::nullopt;
      }
    }
  };

  auto best_time = to_time;
  auto best = std::optional<std::array<journey::leg, 3U>>{};
  auto const update_earliest = [&](auto&& loc_seq, auto&& r) {
    struct enter_info {
      journey::leg ingress_leg_;
      rt::frun fr_;
      stop_idx_t enter_stop_idx_;
      location_idx_t enter_location_;
      unixtime_t enter_time_;
    };

    auto et = std::optional<enter_info>{};
    for (auto x = stop_idx_t{0U}; x != loc_seq.size(); ++x) {
      // Iterate the stop sequence in search direction.
      auto const i =
          static_cast<stop_idx_t>(kFwd ? x : loc_seq.size() - x - 1U);
      auto stp = stop{loc_seq[i]};

      if (et.has_value() &&
          ((q.require_bike_transport_ && !et->fr_[i].bikes_allowed(kExitEv)) ||
           (q.require_car_transport_ && !et->fr_[i].cars_allowed(kExitEv)))) {
        et = std::nullopt;
      }

      // Check for earlier arrival at destination.
      // -> update arrival + legs
      if (et.has_value() && is_dst[to_idx(stp.location_idx())] &&
          (kFwd ? stp.out_allowed() : stp.in_allowed(q.prf_idx_))) {
        auto const trip_time = et->fr_[i].time(kExitEv);

        auto const check_fp = [&](footpath const& fp) {
          if (fp.target() != to) {
            return;
          }

          auto const adjusted_fp_time =
              adjusted_transfer_time(q.transfer_time_settings_, fp.duration());
          auto const dst_time = trip_time + adv(adjusted_fp_time);
          if (!is_better_or_eq(dst_time, best_time)) {
            return;
          }

          best_time = dst_time;
          best = std::array<journey::leg, 3U>{
              et->ingress_leg_,
              journey::leg{
                  SearchDir,
                  et->enter_location_,
                  stp.location_idx(),
                  et->enter_time_,
                  trip_time,
                  journey::run_enter_exit{et->fr_, et->enter_stop_idx_, i},
              },
              journey::leg{
                  SearchDir,
                  stp.location_idx(),
                  to,
                  trip_time,
                  dst_time,
                  footpath{kFwd ? to : stp.location_idx(), adjusted_fp_time},
              }};
        };

        if (opt.to_is_terminal_ &&
            matches(tt, q.dest_match_mode_, to, stp.location_idx())) {
          // Already at the destination complex - getting off is free.
          check_fp({to, duration_t{0}});
        } else {
          check_fp({stp.location_idx(),
                    adjusted_transfer_time(
                        q.transfer_time_settings_,
                        tt.locations_.transfer_time_[stp.location_idx()])});
        }

        auto const& fps = (kFwd ? tt.locations_.footpaths_out_
                                : tt.locations_.footpaths_in_)[q.prf_idx_];
        if (rtt != nullptr && q.prf_idx_ != 0U &&
            (kFwd ? rtt->has_td_footpaths_out_
                  : rtt->has_td_footpaths_in_)[q.prf_idx_]
                [stp.location_idx()]) {
          for_each_footpath<SearchDir>(
              (kFwd ? rtt->td_footpaths_out_
                    : rtt->td_footpaths_in_)[q.prf_idx_][stp.location_idx()],
              trip_time, [&](footpath const fp) { check_fp(fp); });
        } else {
          for (auto const& fp : fps[stp.location_idx()]) {
            check_fp(fp);
          }
        }
      }

      // Check for earlier trip.
      if ((kFwd ? stp.in_allowed(q.prf_idx_) : stp.out_allowed()) &&
          is_src[to_idx(stp.location_idx())]) {
        auto const [fp, location_time] = ingress.at(stp.location_idx());
        auto const candidate = get_earliest(r, i, location_time);
        if (candidate.has_value() &&
            (!et.has_value() || is_better((*candidate)[i].time(kEnterEv),
                                          et->fr_[i].time(kEnterEv)))) {
          et = enter_info{
              .ingress_leg_ =
                  journey::leg{
                      SearchDir,
                      from,
                      stp.location_idx(),
                      from_time,
                      location_time,
                      footpath{kFwd ? stp.location_idx() : from, fp.duration()},
                  },
              .fr_ = *candidate,
              .enter_stop_idx_ = i,
              .enter_location_ = stp.location_idx(),
              .enter_time_ = (*candidate)[i].time(kEnterEv)};
        }
      }
    }
  };

  utl::sorted_diff(
      from_routes, to_routes, std::less<route_idx_t>{},
      [](auto&&, auto&&) { return false; },
      utl::overloaded{
          [](utl::op, route_idx_t) {},
          [&](route_idx_t const r, route_idx_t) {
            if (is_allowed(q.allowed_claszes_, tt.route_clasz_[r]) &&
                (!q.require_bike_transport_ || tt.has_bike_transport(r)) &&
                (!q.require_car_transport_ || tt.has_car_transport(r))) {
              update_earliest(tt.route_location_seq_[r], r);
            }
          }});

  utl::sorted_diff(
      from_rt_transports, to_rt_transports, std::less<rt_transport_idx_t>{},
      [](auto&&, auto&&) { return false; },
      utl::overloaded{
          [](utl::op, rt_transport_idx_t) {},
          [&](rt_transport_idx_t const rt_t, rt_transport_idx_t) {
            if (is_allowed(q.allowed_claszes_,
                           rtt->rt_transport_section_clasz_[rt_t].front()) &&
                (!q.require_bike_transport_ || rtt->has_bike_transport(rt_t)) &&
                (!q.require_car_transport_ || rtt->has_car_transport(rt_t))) {
              update_earliest(rtt->rt_transport_location_seq_[rt_t], rt_t);
            }
          }});

  return best;
}

template std::optional<std::array<journey::leg, 3U>>
get_earliest_alternative<direction::kForward>(timetable const&,
                                              rt_timetable const*,
                                              query const&,
                                              location_idx_t,
                                              location_idx_t,
                                              unixtime_t,
                                              unixtime_t,
                                              bitvec&,
                                              bitvec&,
                                              alternative_options);

template std::optional<std::array<journey::leg, 3U>>
get_earliest_alternative<direction::kBackward>(timetable const&,
                                               rt_timetable const*,
                                               query const&,
                                               location_idx_t,
                                               location_idx_t,
                                               unixtime_t,
                                               unixtime_t,
                                               bitvec&,
                                               bitvec&,
                                               alternative_options);

}  // namespace nigiri::routing
