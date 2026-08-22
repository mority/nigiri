#pragma once

#include "utl/pairwise.h"

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

#include "utl/visit.h"

#include "nigiri/common/delta_t.h"
#include "nigiri/common/interval.h"
#include "nigiri/rt/run.h"
#include "nigiri/rt/service_alert.h"
#include "nigiri/stop.h"
#include "nigiri/string_store.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri {

// If set, the bitfield has to be looked up in the RT timetable.
constexpr auto const kRtBitfieldFlag = std::uint32_t{0x8000'0000U};

using change_callback_t =
    std::function<void(transport const transport,
                       stop_idx_t const stop_idx,
                       event_type const ev_type,
                       std::optional<location_idx_t> const location_idx,
                       std::optional<bool> const in_out_allowed,
                       std::optional<duration_t> const delay)>;

// General note:
// - To deactivate bits for static transports that are updated with delays,
//   rerouting (incl. track changes) or cancellations (without changing the
//   static timetable), it appends a modified copy of the static bitfield to its
//   own `bitfields_` and re-points the `transport_traffic_days_` entry (tagged
//   with `kRtBitfieldFlag`) at it.
// - RT transports represent departure and arrival times relative to a base day.
// - RT transports are currently not grouped into routes to simplify the code.
//   If this leads to performance issues during the routing, grouping into
//   routes can be introduced for real-time routing as well.
// - All RT transports can be resolved via their static transport if they were
//   already scheduled in the static timetable.
// - All RT transports that did not exist in the static timetable, can be looked
//   up with their trip_id in the RT timetable.
struct rt_timetable {
  rt_transport_idx_t add_rt_transport(source_idx_t,
                                      timetable const&,
                                      transport,
                                      std::span<stop::value_type> stop_seq = {},
                                      std::span<delta_t> time_seq = {},
                                      std::string_view new_trip_id = {},
                                      std::string_view route_id = {},
                                      direction_id_t = {},
                                      std::string_view trip_short_name = {},
                                      delta_t = 0);

  delta_t unix_to_delta(unixtime_t const t) const {
    auto const d =
        (t - std::chrono::time_point_cast<unixtime_t::duration>(base_day_))
            .count();
    return clamp(d);
  }

  // Sets one event time. If this is the first event of `rt_t` to move off its
  // scheduled time, the transport is taken off the static scan and put on a
  // real-time scan in the same step -- see mark_deviating().
  void update_time(rt_transport_idx_t rt_t,
                   stop_idx_t,
                   event_type,
                   unixtime_t new_time);

  // Cancels one stop (no boarding, no alighting). Also a deviation: the stop
  // sequence no longer matches the static route's.
  void cancel_stop(rt_transport_idx_t, stop_idx_t);

  // Takes the transport's day off the static scan *and* registers it for a
  // real-time scan. These two always happen together: a transport whose
  // traffic day is cleared while it is in no scan list is invisible to the
  // routing, which is silent data loss rather than a degradation. Keeping the
  // pair inside one function is what makes that state unrepresentable.
  //
  // finalize_rt_transport() may afterwards move the transport into an rt route
  // (faster), or back onto the static scan if it turns out to match the
  // schedule again. Both are optimisations: forgetting to call it costs
  // performance, never journeys.
  void mark_deviating(rt_transport_idx_t);

  // Sets or clears one day of `t` in the rt traffic days, creating the
  // rt-owned bitfield copy on first use.
  void set_rt_traffic_day(transport t, bool active);

  void update_lbs(timetable const& tt,
                  rt_transport_idx_t,
                  stop_idx_t,
                  vector_map<location_idx_t, std::vector<footpath>>&,
                  vector_map<location_idx_t, std::vector<footpath>>&);
  void update_lbs(timetable const& tt);

  void cancel_run(rt::run const&);

  void set_change_callback(change_callback_t callback) {
    change_callback_ = callback;
  }

  void reset_change_callback() { change_callback_ = nullptr; }

  void dispatch_event(rt::run const& r,
                      stop_idx_t const stop_idx,
                      event_type const ev_type,
                      std::optional<location_idx_t> const location_idx,
                      std::optional<bool> const in_out_allowed,
                      std::optional<duration_t> const delay) {
    if (change_callback_ &&
        ((ev_type == event_type::kArr && stop_idx != r.stop_range_.from_) ||
         (ev_type == event_type::kDep && stop_idx != r.stop_range_.to_ - 1))) {
      change_callback_(r.t_, stop_idx, ev_type, location_idx, in_out_allowed,
                       delay);
    }
  }

  void dispatch_delay(rt::run const& r,
                      stop_idx_t const stop_idx,
                      event_type const ev_type,
                      duration_t const delay) {
    dispatch_event(r, stop_idx, ev_type, std::nullopt, std::nullopt, delay);
  }

  void dispatch_stop_change(rt::run const& r,
                            stop_idx_t const stop_idx,
                            event_type const ev_type,
                            std::optional<location_idx_t> const location_idx,
                            std::optional<bool> const in_out_allowed) {
    dispatch_event(r, stop_idx, ev_type, location_idx, in_out_allowed,
                   std::nullopt);
  }

  void set_track(rt_transport_idx_t, stop_idx_t, event_type, std::string_view);
  std::optional<std::string_view> get_track(rt_transport_idx_t,
                                            stop_idx_t,
                                            event_type) const;

  unixtime_t unix_event_time(rt_transport_idx_t const rt_t,
                             stop_idx_t const stop_idx,
                             event_type const ev_type) const {
    return base_day_ +
           std::chrono::minutes{event_time(rt_t, stop_idx, ev_type)};
  }

  delta_t event_time(rt_transport_idx_t const rt_t,
                     stop_idx_t const stop_idx,
                     event_type const ev_type) const {
    auto const ev_idx = stop_idx * 2 - (ev_type == event_type::kArr ? 1 : 0);
    return rt_transport_stop_times_[rt_t][static_cast<unsigned>(ev_idx)];
  }

  std::variant<translation_idx_t, std::string_view> trip_short_name(
      timetable const& tt, rt_transport_idx_t const t) const {
    if (rt_transport_trip_short_names_[t].empty()) {
      return rt_transport_static_transport_[t].apply(utl::overloaded{
          [&](transport const x)
              -> std::variant<translation_idx_t, std::string_view> {
            auto const trip_idx =
                tt.merged_trips_[tt.transport_to_trip_section_[x.t_idx_]
                                     .front()]
                    .front();
            return tt.trip_display_names_[trip_idx];
          },
          [&](rt_add_trip_id_idx_t)
              -> std::variant<translation_idx_t, std::string_view> {
            return std::string_view{"?"};
          }});
    } else {
      return rt_transport_trip_short_names_[t].view();
    }
  }

  std::string_view default_trip_short_name(timetable const& tt,
                                           rt_transport_idx_t const t) const {
    return utl::visit(
        trip_short_name(tt, t),
        [&](translation_idx_t x) { return tt.get_default_translation(x); },
        [](std::string_view x) { return x; });
  }

  std::string_view transport_name(timetable const& tt,
                                  rt_transport_idx_t const t) const;

  debug dbg(timetable const& tt, rt_transport_idx_t const t) const {
    return rt_transport_static_transport_[t].apply(
        utl::overloaded{[&](transport const x) { return tt.dbg(x.t_idx_); },
                        [&](rt_add_trip_id_idx_t) { return debug{"RT"}; }});
  }

  // Call after applying an update to `rt_t`, once its times are final.
  //
  // A real-time update that turns out to change nothing -- a feed covering a
  // trip and reporting it exactly on time, which is what most of a real feed
  // is -- still materialises an rt_transport, and add_rt_transport() still
  // clears the trip's static traffic day. That moves the trip off the static
  // scan onto the per-transport real-time scan, which is far more expensive:
  // the static scan walks a whole route's stop sequence once for all of its
  // transports, the real-time scan walks one stop sequence per transport.
  //
  // For a transport whose times and stops are identical to the schedule, the
  // scheduled and the real-time answer are the same by definition, so the
  // static scan serves both. This restores the traffic day and flags the
  // transport; the routing then leaves flagged transports off
  // rt_transport_mark_ and rides them statically.
  //
  // The rt_transport itself stays, so trip lookup, alerts, tracks and
  // real-time annotation are unaffected -- frun resolves rt_ from the static
  // transport. Idempotent and authoritative in both directions: a transport
  // that was unchanged and is now delayed gets its flag and its traffic day
  // cleared again.
  void finalize_rt_transport(timetable const&, rt_transport_idx_t);

  // True if `rt_t` is identical to its static counterpart *for routing* and is
  // therefore ridden on the static scan. Track-only changes still count as
  // unchanged: they do not affect routing, and frun still surfaces them.
  bool is_unchanged(rt_transport_idx_t const rt_t) const noexcept {
    return to_idx(rt_t) < rt_transport_is_unchanged_.size() &&
           rt_transport_is_unchanged_.test(to_idx(rt_t));
  }

  void set_unchanged(rt_transport_idx_t const rt_t, bool const x) {
    if (to_idx(rt_t) >= rt_transport_is_unchanged_.size()) {
      rt_transport_is_unchanged_.resize(to_idx(rt_t) + 1U);
    }
    rt_transport_is_unchanged_.set(to_idx(rt_t), x);
  }

  bool matches_schedule(timetable const&, rt_transport_idx_t) const;

  // --- rt routes ---------------------------------------------------------
  //
  // The static scan walks a route's stop sequence *once* for all of that
  // route's transports; the real-time scan walks one stop sequence *per*
  // transport, because rt transports are not grouped. For the transports that
  // are still on the real-time scan -- the ones that actually deviate -- that
  // is the dominant cost of a real-time search.
  //
  // An rt route is the same idea as a static route, maintained incrementally:
  // rt transports that share a stop sequence and do not overtake each other,
  // kept ordered by time. Unlike the static timetable the groups are tiny
  // (a handful of transports), so the times stay where they are and the
  // members are just a small sorted list -- no transposed per-route time
  // array.
  //
  // Only transports whose stop sequence still equals their static route's are
  // grouped, which also means they share that route's flags and claszes
  // (add_rt_transport() copies them verbatim), so the routing can filter a
  // whole group at once exactly like a static route. Everything else --
  // additional trips, reroutings, cancelled stops, cancelled runs -- keeps
  // its own per-transport scan.
  //
  // Called from finalize_rt_transport(), i.e. after every update, since an
  // update can change a transport's times and therefore its position (or its
  // eligibility) in a group.
  void regroup_rt_transport(timetable const&, rt_transport_idx_t);

  // Inserts in time order if `rt_t` overtakes neither of its neighbours at any
  // event; false if it does, in which case it belongs in another group.
  bool try_insert_into_rt_route(rt_route_idx_t, rt_transport_idx_t);

  // Registers `rt_t` in location_rt_unrouted_, the list the routing scans
  // per transport. Called once, the first time a transport turns out to need
  // its own scan; an rt transport's *locations* never change after
  // add_rt_transport() -- the only in-place edits to rt_transport_location_seq_
  // cancel a stop, which keeps its location_idx() -- so the registration stays
  // valid for the transport's whole life. A transport that later becomes
  // grouped or unchanged is filtered out when the marks are built.
  void register_unrouted(rt_transport_idx_t);

  // Removes `rt_t` from location_rt_unrouted_ again, once it has a group to be
  // scanned in. Without this every transport that ever deviated would stay in
  // the per-transport list and the routing would walk them all when building
  // its marks -- which is exactly the cost rt routes exist to remove.
  void deregister_unrouted(rt_transport_idx_t);

  rt_route_idx_t rt_route_of(rt_transport_idx_t const rt_t) const noexcept {
    return to_idx(rt_t) < rt_transport_rt_route_.size()
               ? rt_transport_rt_route_[rt_t]
               : rt_route_idx_t::invalid();
  }

  std::uint32_t n_rt_routes() const noexcept {
    return static_cast<std::uint32_t>(rt_route_static_route_.size());
  }

  transport resolve_static(rt_transport_idx_t const rt_t) const noexcept {
    auto const t = rt_transport_static_transport_[rt_t];
    return holds_alternative<transport>(t) ? t.as<transport>() : transport{};
  }

  rt_transport_idx_t resolve_rt(transport const t) const noexcept {
    auto const it = static_trip_lookup_.find(t);
    return it == end(static_trip_lookup_) ? rt_transport_idx_t::invalid()
                                          : it->second;
  }

  std::uint32_t n_rt_transports() const noexcept {
    return rt_transport_src_.size();
  }

  bool is_flag_set(route_flag const f, rt_transport_idx_t const r) const {
    return rt_transport_flags_[f][to_idx(r) * 2U] ||
           rt_transport_flags_[f][to_idx(r) * 2U + 1U];
  }

  bitfield const& traffic_days(bitfield_idx_t const i) const {
    return (to_idx(i) & kRtBitfieldFlag) != 0U
               ? bitfields_[bitfield_idx_t{to_idx(i) & ~kRtBitfieldFlag}]
               : tt_->bitfields_[i];
  }

  bitfield_idx_t rt_bitfield_idx() const {
    return bitfield_idx_t{static_cast<bitfield_idx_t::value_t>(
        (bitfields_.size() - 1U) | kRtBitfieldFlag)};
  }

  bool is_transport_active(transport_idx_t const t, day_idx_t const day) const {
    return traffic_days(transport_traffic_days_[t]).test(to_idx(day));
  }

  timetable const* tt_{nullptr};

  array<bitvec_map<location_idx_t>, kNProfiles> has_td_footpaths_out_;
  array<bitvec_map<location_idx_t>, kNProfiles> has_td_footpaths_in_;
  array<vecvec<location_idx_t, td_footpath>, kNProfiles> td_footpaths_out_;
  array<vecvec<location_idx_t, td_footpath>, kNProfiles> td_footpaths_in_;

  // Updated transport traffic days from the static timetable.
  // Initial: 100% copy from static, then adapted according to real-time
  // updates
  vector_map<transport_idx_t, bitfield_idx_t> transport_traffic_days_;
  vector_map<bitfield_idx_t, bitfield> bitfields_;

  // Location -> RT transports that stop at this location
  mutable_fws_multimap<location_idx_t, rt_transport_idx_t>
      location_rt_transports_;

  // Base-day: all real-time timestamps (departures + arrivals in
  // rt_transport_stop_times_) are given relative to this base day.
  date::sys_days base_day_;
  day_idx_t base_day_idx_;

  // Lookup: static transport -> realtime transport
  // only works for transport that existed in the static timetable
  hash_map<transport, rt_transport_idx_t> static_trip_lookup_;

  // RT transport -> static transport (not for additional trips)
  vector_map<rt_transport_idx_t, variant<transport, rt_add_trip_id_idx_t>>
      rt_transport_static_transport_;

  struct additional_trips {
    string_store<rt_add_trip_id_idx_t> ids_;
    vector_map<rt_add_trip_id_idx_t, rt_transport_idx_t> transports_;
  };
  vector_map<source_idx_t, additional_trips> additional_trips_;

  vector_map<rt_transport_idx_t, source_idx_t> rt_transport_src_;

  bitvec_map<rt_transport_idx_t> rt_transport_direction_id_;
  vector_map<rt_transport_idx_t, route_id_idx_t> rt_transport_route_id_;

  // RT transport -> direction for each section
  vecvec<trip_direction_string_idx_t, char> rt_transport_direction_strings_;
  vecvec<rt_transport_idx_t, trip_direction_string_idx_t>
      rt_transport_section_directions_;

  // RT transport -> event times (dep, arr, dep, arr, ...)
  vecvec<rt_transport_idx_t, delta_t> rt_transport_stop_times_;
  vecvec<rt_transport_idx_t, stop::value_type> rt_transport_location_seq_;

  // RT transport -> real-time track (dep, arr, dep, arr, ...)
  // - empty if no overwrite exists at all
  // - track_idx_t::invalid for stops where no overwrite exists
  vecvec<rt_transport_idx_t, track_idx_t> rt_transport_track_sequence_;
  string_store<track_idx_t> track_strings_;

  // RT trip index -> display name (empty if not changed)
  vecvec<rt_transport_idx_t, char> rt_transport_trip_short_names_;
  vecvec<rt_transport_idx_t, char> rt_transport_line_;

  // RT transport -> vehicle clasz for each section
  vecvec<rt_transport_idx_t, clasz> rt_transport_section_clasz_;

  // RT transport -> canceled flag
  bitvec rt_transport_is_cancelled_;

  // Set for rt transports that are identical to their static counterpart, see
  // finalize_rt_transport(). Empty until that has run.
  bitvec rt_transport_is_unchanged_;

  // rt routes, see regroup_rt_transport(). Members are ordered by time and
  // free of overtaking; the stop sequence is the static route's.
  vector_map<rt_route_idx_t, std::vector<rt_transport_idx_t>>
      rt_route_transports_;
  vector_map<rt_route_idx_t, route_idx_t> rt_route_static_route_;
  vector_map<rt_transport_idx_t, rt_route_idx_t> rt_transport_rt_route_;
  hash_map<route_idx_t, std::vector<rt_route_idx_t>> rt_routes_by_static_route_;
  mutable_fws_multimap<location_idx_t, rt_route_idx_t> location_rt_routes_;

  // The rt transports that are scanned one by one because they do not fit a
  // group: additional trips, reroutings, cancelled stops, cancelled runs.
  // Together with location_rt_routes_ this replaces walking
  // location_rt_transports_ when the routing builds its marks.
  // A plain vector per location, not a mutable_fws_multimap: entries have to
  // be removable again (see deregister_unrouted).
  vector_map<location_idx_t, std::vector<rt_transport_idx_t>>
      location_rt_unrouted_;
  bitvec rt_transport_unrouted_registered_;

  // RT transport * 2 -> flags (bikes, cars, wheelchairs, reservtion) along the
  // transport RT transport * 2 + 1 -> flags along parts of the transport
  std::array<bitvec, kNumRouteFlags> rt_transport_flags_;

  // RT transport -> flags for each section
  std::array<vecvec<rt_transport_idx_t, bool>, kNumRouteFlags>
      rt_flags_per_section_;

  // Service alerts
  alerts alerts_;

  change_callback_t change_callback_;

  // Lower bound graph extension.
  bitvec_map<location_idx_t> fwd_search_lb_graph_has_edges_;
  bitvec_map<location_idx_t> bwd_search_lb_graph_has_edges_;
  vecvec<location_idx_t, footpath> fwd_search_lb_graph_;
  vecvec<location_idx_t, footpath> bwd_search_lb_graph_;

  // Incremental mode requires copyability of rt_timetable.
  // unique_ptr alone would make the rt_timetable move only (not copyable)
  // -> we need a copyable wrapper
  struct gpu_rtt_slot {
    gpu_rtt_slot() = default;
    gpu_rtt_slot(gpu_rtt_slot const&) { /* start empty, upload after update */ }
    gpu_rtt_slot(gpu_rtt_slot&&) = default;
    gpu_rtt_slot& operator=(gpu_rtt_slot&&) = default;
    std::unique_ptr<void, void (*)(void*)> ptr_{nullptr, [](void*) {}};
  };
  gpu_rtt_slot gpu_rtt_;
};

}  // namespace nigiri
