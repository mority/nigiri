#include "nigiri/routing/raptor/reconstruct.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

#include "utl/erase_duplicates.h"
#include "utl/helpers/algorithm.h"
#include "utl/overloaded.h"

#include "nigiri/common/it_range.h"
#include "nigiri/for_each_meta.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/search_location.h"
#include "nigiri/routing/transfers.h"
#include "nigiri/rt/frun.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

trip_idx_t trip_at(rt::run_stop const& stp, event_type const ev_type) {
  return stp.fr_->is_scheduled() ? stp.get_trip_idx(ev_type)
                                 : trip_idx_t::invalid();
}

void optimize_initial_departure(timetable const& tt,
                                rt_timetable const* rtt,
                                query const& q,
                                journey& j) {
  if (j.legs_.size() <= 1 || !holds_alternative<offset>(j.legs_[0].uses_) ||
      !holds_alternative<journey::run_enter_exit>(j.legs_[1].uses_)) {
    return;
  }

  auto& offset_leg = j.legs_[0];
  auto& transport_leg = j.legs_[1];
  auto& ree = get<journey::run_enter_exit>(transport_leg.uses_);
  auto offset_dur_best = get<offset>(offset_leg.uses_).duration();

  auto const& offsets = q.start_;

  auto r = rt::run{ree.r_};
  r.stop_range_ = {0U, static_cast<stop_idx_t>(ree.stop_range_.to_ - 1U)};
  for (auto const stp : rt::frun{tt, rtt, r}) {
    if (!q.via_stops_.empty() &&
        matches(tt, nullptr, location_match_mode::kEquivalent,
                stp.get_location_idx(), q.via_stops_.front().location_)) {
      // don't skip over via stops
      break;
    }
    if (!stp.in_allowed()) {
      continue;
    }
    for (auto const& o : offsets) {
      if (offset_dur_best <= o.duration() ||
          !matches(tt, nullptr, location_match_mode::kExact, o.target(),
                   stp.get_location_idx())) {
        continue;
      }

      auto const dep = stp.time(event_type::kDep);
      auto const o_start = dep - o.duration();
      if (offset_leg.dep_time_ <= o_start) {
        offset_leg.to_ = stp.get_location_idx();
        offset_leg.dep_time_ = o_start;
        offset_leg.arr_time_ = dep;
        offset_leg.uses_ = o;
        transport_leg.from_ = stp.get_location_idx();
        transport_leg.dep_time_ = dep;
        ree.stop_range_.from_ = stp.stop_idx_;
        offset_dur_best = o.duration();
      }
    }
  }
}

location_idx_t exact_location(location_match_mode const mode,
                              std::vector<offset> const& offsets,
                              location_idx_t const l) {
  return mode == location_match_mode::kExact && offsets.size() == 1U
             ? offsets.front().target()
             : l;
}

void optimize_initial_start_footpath(timetable const& tt,
                                     rt_timetable const* rtt,
                                     query const& q,
                                     journey& j) {
  if (j.legs_.size() <= 1 ||
      !holds_alternative<footpath>(j.legs_.front().uses_) ||
      !holds_alternative<journey::run_enter_exit>(j.legs_[1].uses_)) {
    return;
  }

  auto& fp_leg = j.legs_.front();
  auto& transport_leg = j.legs_[1];
  auto& ree = get<journey::run_enter_exit>(transport_leg.uses_);

  auto const start_loc =
      exact_location(q.start_match_mode_, q.start_, fp_leg.from_);

  if (is_td_source(rtt, q.prf_idx_, start_loc)) {
    return;  // TODO(felix) handle shortening of td-footpaths
  }

  auto fp_dur_best = adjusted_transfer_time(
      q.transfer_time_settings_, get<footpath>(fp_leg.uses_).duration());

  auto const footpaths =
      get_transfers<direction::kForward>(tt, rtt, q.prf_idx_, start_loc);

  auto r = rt::run{ree.r_};
  r.stop_range_ = {0U, static_cast<stop_idx_t>(ree.stop_range_.to_ - 1U)};
  for (auto const stp : rt::frun{tt, rtt, r}) {
    if (!q.via_stops_.empty() &&
        matches(tt, nullptr, location_match_mode::kEquivalent,
                stp.get_location_idx(), q.via_stops_[0].location_)) {
      break;  // don't skip over via stops
    }

    if (!stp.in_allowed()) {
      continue;
    }

    auto const l = search_location(q.prf_idx_, stp);
    for (auto const& fp : footpaths) {
      if (fp.target() != l) {
        continue;
      }
      auto const fp_dur =
          adjusted_transfer_time(q.transfer_time_settings_, fp.duration());
      if (fp_dur < fp_dur_best) {
        auto const dep = stp.time(event_type::kDep);
        auto const fp_start = dep - fp_dur;
        if (fp_leg.dep_time_ <= fp_start) {
          fp_leg.to_ = stp.get_location_idx();
          fp_leg.dep_time_ = fp_start;
          fp_leg.arr_time_ = dep;
          fp_leg.uses_ = footpath{stp.get_location_idx(), fp.duration()};
          transport_leg.from_ = stp.get_location_idx();
          transport_leg.dep_time_ = dep;
          ree.stop_range_.from_ = stp.stop_idx_;
          fp_dur_best = fp_dur;
        }
      }
    }
  }
}

void optimize_last_arrival(timetable const& tt,
                           rt_timetable const* rtt,
                           query const& q,
                           journey& j) {

  if (j.legs_.size() <= 1 || !holds_alternative<offset>(j.legs_.back().uses_) ||
      !holds_alternative<journey::run_enter_exit>(rbegin(j.legs_)[1].uses_)) {
    return;
  }

  auto& offset_leg = j.legs_.back();
  auto& transport_leg = rbegin(j.legs_)[1];
  auto& ree = get<journey::run_enter_exit>(transport_leg.uses_);
  auto offset_dur_best = get<offset>(offset_leg.uses_).duration();

  auto const* offsets = &q.destination_;

  auto fr = rt::frun{tt, rtt, ree.r_};
  auto range_from = static_cast<stop_idx_t>(ree.stop_range_.from_ + 1U);

  if (!q.via_stops_.empty()) {
    // don't skip the last via stop
    for (auto i = stop_idx_t{0U}; i < fr.size(); ++i) {
      auto idx = static_cast<stop_idx_t>(fr.size() - i - 1U);
      if (matches(tt, nullptr, location_match_mode::kEquivalent,
                  fr[idx].get_location_idx(), q.via_stops_.back().location_)) {
        range_from = std::max(range_from, idx);
        break;
      }
    }
  }

  fr.stop_range_ = {range_from, fr.size()};

  for (auto const stp : fr) {
    if (!stp.out_allowed()) {
      continue;
    }
    for (auto const& o : *offsets) {
      if (offset_dur_best <= o.duration() ||
          !matches(tt, nullptr, location_match_mode::kExact, o.target(),
                   stp.get_location_idx())) {
        continue;
      }
      auto const arr = stp.time(event_type::kArr);
      auto const o_end = arr + o.duration();
      if (o_end <= offset_leg.arr_time_) {
        offset_leg.from_ = stp.get_location_idx();
        offset_leg.dep_time_ = arr;
        offset_leg.arr_time_ = o_end;
        offset_leg.uses_ = o;
        transport_leg.to_ = stp.get_location_idx();
        transport_leg.arr_time_ = arr;
        ree.stop_range_.to_ = stp.stop_idx_ + 1U;
        offset_dur_best = o.duration();
      }
    }
  }
}

void optimize_final_egress_footpath(timetable const& tt,
                                    rt_timetable const* rtt,
                                    query const& q,
                                    journey& j) {
  if (j.legs_.size() <= 1 ||
      !holds_alternative<footpath>(j.legs_.back().uses_) ||
      !holds_alternative<journey::run_enter_exit>(rbegin(j.legs_)[1].uses_)) {
    return;
  }

  auto& fp_leg = j.legs_.back();
  auto& transport_leg = rbegin(j.legs_)[1];
  auto& ree = get<journey::run_enter_exit>(transport_leg.uses_);

  auto const dest_loc =
      exact_location(q.dest_match_mode_, q.destination_, fp_leg.to_);

  if (rtt != nullptr && has_profile_td_footpaths(q.prf_idx_) &&
      rtt->has_td_footpaths_in_[q.prf_idx_].test(dest_loc)) {
    return;  // TODO(felix) handle shortening of td-footpaths
  }

  auto fp_dur_best = adjusted_transfer_time(
      q.transfer_time_settings_, get<footpath>(fp_leg.uses_).duration());

  auto const footpaths =
      get_transfers<direction::kBackward>(tt, rtt, q.prf_idx_, dest_loc);

  auto fr = rt::frun{tt, rtt, ree.r_};
  auto range_from = static_cast<stop_idx_t>(ree.stop_range_.from_ + 1U);

  if (!q.via_stops_.empty()) {
    // don't alight before the last via stop
    auto const& last_via = q.via_stops_.back();
    for (auto i = stop_idx_t{0U}; i < fr.size(); ++i) {
      auto idx = static_cast<stop_idx_t>(fr.size() - i - 1U);
      if (matches(tt, nullptr, location_match_mode::kEquivalent,
                  fr[idx].get_location_idx(), last_via.location_)) {
        range_from = std::max(range_from, idx);
        break;
      }
    }
  }

  fr.stop_range_ = {range_from, fr.size()};

  for (auto const stp : fr) {
    if (!stp.out_allowed()) {
      continue;
    }
    auto const l = search_location(q.prf_idx_, stp);
    for (auto const& fp : footpaths) {
      if (fp.target() != l) {
        continue;
      }
      auto const fp_dur =
          adjusted_transfer_time(q.transfer_time_settings_, fp.duration());
      if (fp_dur < fp_dur_best) {
        auto const arr = stp.time(event_type::kArr);
        auto const fp_end = arr + fp_dur;
        if (fp_end <= fp_leg.arr_time_) {
          fp_leg.from_ = stp.get_location_idx();
          fp_leg.dep_time_ = arr;
          fp_leg.arr_time_ = fp_end;
          fp_leg.uses_ = footpath{fp_leg.to_, fp.duration()};
          transport_leg.to_ = stp.get_location_idx();
          transport_leg.arr_time_ = arr;
          ree.stop_range_.to_ = stp.stop_idx_ + 1U;
          fp_dur_best = fp_dur;
        }
      }
    }
  }
}

bool is_preferred_transfer(timetable const& tt,
                           location_idx_t const from,
                           location_idx_t const to,
                           trip_idx_t const from_trip,
                           trip_idx_t const to_trip) {
  auto const& preferred = tt.locations_.preferred_transfers_;
  auto const route_of = [&](trip_idx_t const t) {
    return t == trip_idx_t::invalid() ? route_id_idx_t::invalid()
                                      : tt.trip_route_id_[t];
  };
  auto const is_side_match = [&](trip_idx_t const rule_trip,
                                 route_id_idx_t const rule_route,
                                 trip_idx_t const observed) {
    if (rule_trip != trip_idx_t::invalid()) {
      return rule_trip == observed;
    }
    if (rule_route != route_id_idx_t::invalid()) {
      return rule_route == route_of(observed);
    }
    return true;
  };

  for (auto f = from; f != location_idx_t::invalid();
       f = tt.locations_.parents_[f]) {
    if (f < preferred.size() &&
        utl::any_of(
            preferred[f],
            [&](auto const& p) {
              return is_side_match(p.from_trip_, p.from_route_, from_trip) &&
                     is_side_match(p.to_trip_, p.to_route_, to_trip) &&
                     tt.locations_.is_self_or_parent(p.to_, to);
            })) {
      return true;
    }
  }
  return false;
}

double get_penalty(timetable const& tt,
                   duration_t const duration,
                   duration_t const buffer,
                   location_idx_t const from,
                   location_idx_t const to,
                   trip_idx_t const from_trip,
                   trip_idx_t const to_trip) {
  // Basic weight: footpath duration.
  auto weight = static_cast<double>(duration.count());

  // Adjust for station size.
  if (matches(tt, nullptr, location_match_mode::kEquivalent, from, to)) {
    auto const x = tt.locations_.get_root_idx(from);
    weight -= (static_cast<double>(tt.locations_.location_importance_[x]) /
               tt.locations_.max_importance_) *
              3U;
  }

  // Consider transfer time buffer (clamped + weighted)
  constexpr auto kMaxRewardedBuffer = duration_t{15};
  constexpr auto kBufferWeight = 0.2;
  weight -= kBufferWeight *
            static_cast<double>(
                std::clamp(buffer, duration_t{0}, kMaxRewardedBuffer).count());

  constexpr auto kPreferredBonus = 3.0;
  if (is_preferred_transfer(tt, from, to, from_trip, to_trip)) {
    weight -= kPreferredBonus;
  }

  return weight;
}

void optimize_transfers(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q,
                        journey& j) {
  struct change {
    stop_idx_t exit_, enter_;
    location_idx_t exit_loc_, enter_loc_;
    unixtime_t arr_, dep_, arr_fp_;
    footpath fp_;
    double penalty_;
    bool same_place_;
  };

  struct transfer {
    std::size_t leg_;
    bool fixed_;
    std::vector<change> changes_;
  };

  struct node {
    stop_idx_t enter_, exit_;
    double cost_{std::numeric_limits<double>::max()};
    std::size_t pred_{0U};
    std::size_t change_{0U};
  };

  constexpr auto kEps = 1e-9;
  constexpr auto kUnreached = std::numeric_limits<double>::max();

  auto const is_transfer = [&](std::size_t const i) {
    return i + 2U < j.legs_.size() &&
           holds_alternative<journey::run_enter_exit>(j.legs_[i].uses_) &&
           holds_alternative<footpath>(j.legs_[i + 1U].uses_) &&
           holds_alternative<journey::run_enter_exit>(j.legs_[i + 2U].uses_);
  };
  auto const ree = [&](std::size_t const i) -> journey::run_enter_exit& {
    return get<journey::run_enter_exit>(j.legs_[i].uses_);
  };

  auto const vias = count_vias(tt, rtt, q, j);

  auto const collect = [&](std::size_t const i, stop_idx_t const min_enter,
                           bool const last) {
    auto const& leg_from = j.legs_[i];
    auto const& leg_footpath = j.legs_[i + 1U];
    auto const& leg_to = j.legs_[i + 2U];
    auto const& ree_from = ree(i);
    auto const& ree_to = ree(i + 2U);

    auto t = transfer{.leg_ = i, .fixed_ = false, .changes_ = {}};

    auto current_v = vias.before_[i];
    auto keep_transfer = false;

    auto fr_from = rt::frun{tt, rtt, ree_from.r_};
    auto from_start = static_cast<stop_idx_t>(ree_from.stop_range_.from_ + 1U);
    for (auto s = ree_from.stop_range_.from_; s < fr_from.size(); ++s) {
      if (current_v < q.via_stops_.size() &&
          matches(tt, nullptr, location_match_mode::kEquivalent,
                  fr_from[s].get_location_idx(),
                  q.via_stops_[current_v].location_)) {
        if (q.via_stops_[current_v].stay_ == 0_minutes) {
          from_start = std::max(from_start, s);
          ++current_v;
        } else {
          keep_transfer = true;
          break;
        }
      }
    }

    auto fr_to = rt::frun{tt, rtt, ree_to.r_};
    auto to_end = static_cast<stop_idx_t>(ree_to.stop_range_.to_ - 1U);
    for (auto s = ree_to.stop_range_.from_; s < fr_to.size(); ++s) {
      if (current_v < q.via_stops_.size() &&
          matches(tt, nullptr, location_match_mode::kEquivalent,
                  fr_to[s].get_location_idx(),
                  q.via_stops_[current_v].location_)) {
        if (q.via_stops_[current_v].stay_ == 0_minutes) {
          to_end = std::min(to_end, static_cast<stop_idx_t>(s + 1U));
          ++current_v;
        } else {
          keep_transfer = true;
          break;
        }
      }
    }

    if (q.via_stops_.empty()) {
      from_start = static_cast<stop_idx_t>(min_enter + 1U);
      if (!last) {
        to_end = static_cast<stop_idx_t>(fr_to.size() - 1U);
      }
    }

    if (keep_transfer || is_td_source(rtt, q.prf_idx_, leg_footpath.from_)) {
      t.fixed_ = true;
      t.changes_.push_back(
          {.exit_ = static_cast<stop_idx_t>(ree_from.stop_range_.to_ - 1U),
           .enter_ = ree_to.stop_range_.from_,
           .exit_loc_ = leg_from.to_,
           .enter_loc_ = leg_to.from_,
           .arr_ = leg_from.arr_time_,
           .dep_ = leg_to.dep_time_,
           .arr_fp_ = leg_footpath.arr_time_,
           .fp_ = get<footpath>(leg_footpath.uses_),
           .penalty_ = 0.0,
           .same_place_ = false});
      return t;
    }

    fr_from.stop_range_ = {from_start, fr_from.size()};
    fr_to.stop_range_ = {stop_idx_t{0U}, to_end};
    for (auto const stp_from : fr_from) {
      if (!stp_from.out_allowed()) {
        continue;
      }
      auto const from_trip = trip_at(stp_from, event_type::kArr);
      for (auto const stp_to : it_range{fr_to.rbegin(), fr_to.rend()}) {
        if (!stp_to.in_allowed()) {
          continue;
        }
        auto const shortest = shortest_transfer(
            tt, rtt, q.prf_idx_, search_location(q.prf_idx_, stp_from),
            search_location(q.prf_idx_, stp_to));
        if (!shortest.has_value()) {
          continue;
        }
        auto const fp = footpath{stp_to.get_location_idx(), *shortest};
        auto const fp_dur =
            adjusted_transfer_time(q.transfer_time_settings_, fp.duration());
        auto const arr = stp_from.time(event_type::kArr);
        auto const dep = stp_to.time(event_type::kDep);
        auto const arr_fp = arr + fp_dur;
        if (arr_fp > dep) {
          continue;
        }
        t.changes_.push_back(
            {.exit_ = stp_from.stop_idx_,
             .enter_ = stp_to.stop_idx_,
             .exit_loc_ = stp_from.get_location_idx(),
             .enter_loc_ = stp_to.get_location_idx(),
             .arr_ = arr,
             .dep_ = dep,
             .arr_fp_ = arr_fp,
             .fp_ = fp,
             .penalty_ = get_penalty(tt, fp_dur, dep - arr_fp,
                                     stp_from.get_location_idx(),
                                     stp_to.get_location_idx(), from_trip,
                                     trip_at(stp_to, event_type::kDep)),
             .same_place_ = matches(
                 tt, nullptr, location_match_mode::kEquivalent,
                 stp_from.get_location_idx(), stp_to.get_location_idx())});
      }
    }
    return t;
  };

  auto const apply = [&](std::size_t const i, change const& c) {
    auto& leg_from = j.legs_[i];
    auto& leg_footpath = j.legs_[i + 1U];
    auto& leg_to = j.legs_[i + 2U];

    leg_from.to_ = c.exit_loc_;
    leg_from.arr_time_ = c.arr_;
    ree(i).stop_range_.to_ = static_cast<stop_idx_t>(c.exit_ + 1U);

    leg_to.from_ = c.enter_loc_;
    leg_to.dep_time_ = c.dep_;
    ree(i + 2U).stop_range_.from_ = c.enter_;

    leg_footpath.from_ = c.exit_loc_;
    leg_footpath.to_ = c.enter_loc_;
    leg_footpath.dep_time_ = c.arr_;
    leg_footpath.arr_time_ = c.arr_fp_;
    leg_footpath.uses_ = c.fp_;
  };

  auto const solve = [&](std::vector<transfer> const& chain) {
    auto const n_trips = chain.size() + 1U;
    auto nodes = std::vector<std::vector<node>>(n_trips);
    for (auto k = 0U; k != n_trips; ++k) {
      auto enters = std::vector<stop_idx_t>{};
      auto exits = std::vector<stop_idx_t>{};
      if (k == 0U) {
        enters.push_back(ree(chain.front().leg_).stop_range_.from_);
      } else {
        for (auto const& c : chain[k - 1U].changes_) {
          enters.push_back(c.enter_);
        }
      }
      if (k + 1U == n_trips) {
        exits.push_back(static_cast<stop_idx_t>(
            ree(chain.back().leg_ + 2U).stop_range_.to_ - 1U));
      } else {
        for (auto const& c : chain[k].changes_) {
          exits.push_back(c.exit_);
        }
      }
      utl::erase_duplicates(enters);
      utl::erase_duplicates(exits);
      for (auto const e : enters) {
        for (auto const x : exits) {
          if (e < x) {
            nodes[k].push_back({.enter_ = e, .exit_ = x});
          }
        }
      }
    }

    for (auto& n : nodes.front()) {
      n.cost_ = 0.0;
    }
    for (auto k = 0U; k != chain.size(); ++k) {
      auto const& t = chain[k];
      for (auto ci = 0U; ci != t.changes_.size(); ++ci) {
        auto const& c = t.changes_[ci];
        for (auto pi = 0U; pi != nodes[k].size(); ++pi) {
          auto const& p = nodes[k][pi];
          if (p.exit_ != c.exit_ || p.cost_ == kUnreached) {
            continue;
          }
          for (auto& n : nodes[k + 1U]) {
            if (n.enter_ != c.enter_) {
              continue;
            }
            auto const is_double_visit =
                !t.fixed_ && utl::any_of(t.changes_, [&](change const& d) {
                  return d.same_place_ && p.enter_ < d.exit_ &&
                         d.exit_ <= c.exit_ && c.enter_ <= d.enter_ &&
                         d.enter_ < n.exit_ &&
                         (d.exit_ != c.exit_ || d.enter_ != c.enter_);
                });
            if (is_double_visit) {
              continue;
            }
            auto const cost = p.cost_ + c.penalty_;
            if (cost < n.cost_ - kEps) {
              n.cost_ = cost;
              n.pred_ = pi;
              n.change_ = ci;
            }
          }
        }
      }
    }

    auto const& last = nodes.back();
    auto best = last.size();
    for (auto ni = 0U; ni != last.size(); ++ni) {
      if (last[ni].cost_ == kUnreached) {
        continue;
      }
      if (best == last.size() || last[ni].cost_ < last[best].cost_ - kEps ||
          (last[ni].cost_ <= last[best].cost_ + kEps &&
           last[ni].change_ < last[best].change_)) {
        best = ni;
      }
    }
    if (best == last.size()) {
      return;
    }
    for (auto k = chain.size(); k != 0U; --k) {
      auto const& n = nodes[k][best];
      auto const& t = chain[k - 1U];
      if (!t.fixed_) {
        apply(t.leg_, t.changes_[n.change_]);
      }
      best = n.pred_;
    }
  };

  auto chain = std::vector<transfer>{};
  for (auto i = 0U; i < j.legs_.size(); ++i) {
    if (!is_transfer(i)) {
      continue;
    }
    chain.clear();
    auto min_enter = ree(i).stop_range_.from_;
    for (; is_transfer(i); i += 2U) {
      chain.push_back(collect(i, min_enter, !is_transfer(i + 2U)));
      auto const& changes = chain.back().changes_;
      min_enter = changes.empty()
                      ? ree(i + 2U).stop_range_.from_
                      : utl::min_element(changes, [](auto&& a, auto&& b) {
                          return a.enter_ < b.enter_;
                        })->enter_;
    }
    solve(chain);
  }
}

void optimize_footpaths(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q,
                        journey& j) {
  optimize_initial_departure(tt, rtt, q, j);
  optimize_initial_start_footpath(tt, rtt, q, j);
  optimize_last_arrival(tt, rtt, q, j);
  optimize_final_egress_footpath(tt, rtt, q, j);
  optimize_transfers(tt, rtt, q, j);
}

}  // namespace nigiri::routing
