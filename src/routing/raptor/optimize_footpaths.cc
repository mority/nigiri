#include "nigiri/routing/raptor/reconstruct.h"

#include "nigiri/routing/for_each_meta.h"
#include "nigiri/routing/journey.h"
#include "nigiri/rt/frun.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

template <direction SearchDir>
void optimize_start(timetable const& tt,
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

  auto const* offsets = &q.start_;
  if constexpr (SearchDir == direction::kBackward) {
    offsets = &q.destination_;
  }

  for (auto const& o : *offsets) {
    if (offset_dur_best <= o.duration()) {
      continue;
    }

    auto const fr = (rtt != nullptr && rtt->resolve_rt(ree.r_.t_) !=
                                           rt_transport_idx_t::invalid())
                        ? rt::frun::from_rt(tt, rtt, rtt->resolve_rt(ree.r_.t_))
                        : rt::frun::from_t(tt, rtt, ree.r_.t_);

    for (auto stop_idx = stop_idx_t{0U}; stop_idx != ree.stop_range_.to_ - 1U;
         ++stop_idx) {
      auto stp = fr[stop_idx];
      if (!stp.in_allowed() || !matches(tt, location_match_mode::kExact,
                                        o.target(), stp.get_location_idx())) {
        continue;
      }
      auto const dep =
          rtt != nullptr &&
                  rtt->resolve_rt(ree.r_.t_) != rt_transport_idx_t::invalid()
              ? rtt->unix_event_time(rtt->resolve_rt(ree.r_.t_), stop_idx,
                                     event_type::kDep)
              : tt.event_time(ree.r_.t_, stop_idx, event_type::kDep);
      auto const o_start = dep - o.duration();
      if (offset_leg.dep_time_ <= o_start) {
        offset_leg.to_ = stp.get_location_idx();
        offset_leg.dep_time_ = o_start;
        offset_leg.arr_time_ = dep;
        offset_leg.uses_ = o;
        transport_leg.from_ = stp.get_location_idx();
        transport_leg.dep_time_ = dep;
        ree.stop_range_.from_ = stop_idx;
        offset_dur_best = o.duration();
      }
    }
  }
}

template <direction SearchDir>
void optimize_end(timetable const& tt,
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
  if constexpr (SearchDir == direction::kBackward) {
    offsets = &q.start_;
  }
  
  for (auto const& o : *offsets) {
    if (offset_dur_best <= o.duration()) {
      continue;
    }

    auto const fr = (rtt != nullptr && rtt->resolve_rt(ree.r_.t_) !=
                                           rt_transport_idx_t::invalid())
                        ? rt::frun::from_rt(tt, rtt, rtt->resolve_rt(ree.r_.t_))
                        : rt::frun::from_t(tt, rtt, ree.r_.t_);

    for (auto stop_idx = static_cast<stop_idx_t>(ree.stop_range_.from_ + 1U);
         stop_idx != fr.size(); ++stop_idx) {
      auto stp = fr[stop_idx];
      if (!stp.out_allowed() || !matches(tt, location_match_mode::kExact,
                                         o.target(), stp.get_location_idx())) {
        continue;
      }
      auto const arr =
          rtt != nullptr &&
                  rtt->resolve_rt(ree.r_.t_) != rt_transport_idx_t::invalid()
              ? rtt->unix_event_time(rtt->resolve_rt(ree.r_.t_), stop_idx,
                                     event_type::kArr)
              : tt.event_time(ree.r_.t_, stop_idx, event_type::kArr);
      auto const o_end = arr + o.duration();
      if (o_end <= offset_leg.arr_time_) {
        offset_leg.from_ = stp.get_location_idx();
        offset_leg.dep_time_ = arr;
        offset_leg.arr_time_ = o_end;
        offset_leg.uses_ = o;
        transport_leg.to_ = stp.get_location_idx();
        transport_leg.arr_time_ = arr;
        ree.stop_range_.to_ = stop_idx + 1U;
        offset_dur_best = o.duration();
      }
    }
  }
}

void optimize_transfers(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q,
                        journey& j) {
  for (auto i = 0U; i + 2 < j.legs_.size(); ++i) {
    auto& leg = j.legs_[i];
    auto& leg_footpath = j.legs_[i + 1];
    auto& leg_transfer = j.legs_[i + 2];
    if (!holds_alternative<journey::run_enter_exit>(leg.uses_) ||
        !holds_alternative<footpath>(j.legs_[i + 1].uses_) ||
        !holds_alternative<journey::run_enter_exit>(j.legs_[i + 2].uses_) ||
        matches(tt, location_match_mode::kEquivalent, leg.to_,
                leg_transfer.from_)) {
      continue;
    }
    auto fp_dur_best = get<footpath>(leg_footpath.uses_).duration();
    auto& ree = get<journey::run_enter_exit>(leg.uses_);
    auto& ree_transfer = get<journey::run_enter_exit>(leg_transfer.uses_);

    auto const fr = (rtt != nullptr && rtt->resolve_rt(ree.r_.t_) !=
                                           rt_transport_idx_t::invalid())
                        ? rt::frun::from_rt(tt, rtt, rtt->resolve_rt(ree.r_.t_))
                        : rt::frun::from_t(tt, rtt, ree.r_.t_);
    auto const fr_transfer =
        (rtt != nullptr &&
         rtt->resolve_rt(ree_transfer.r_.t_) != rt_transport_idx_t::invalid())
            ? rt::frun::from_rt(tt, rtt, rtt->resolve_rt(ree_transfer.r_.t_))
            : rt::frun::from_t(tt, rtt, ree_transfer.r_.t_);

    for (auto stop_idx = static_cast<stop_idx_t>(ree.stop_range_.from_ + 1U);
         stop_idx != fr.size(); ++stop_idx) {
      auto const stp = fr[stop_idx];
      if (!stp.out_allowed()) {
        continue;
      }
      auto const footpaths =
          tt.locations_.footpaths_out_[q.prf_idx_][stp.get_location_idx()];
      for (auto const& fp : footpaths) {
        if (fp.duration() >= fp_dur_best) {
          continue;
        }

        for (auto stop_idx_transfer = stop_idx_t{0U};
             stop_idx_transfer != ree_transfer.stop_range_.to_ - 1U;
             ++stop_idx_transfer) {
          auto const stp_transfer = fr_transfer[stop_idx_transfer];
          if (!stp_transfer.in_allowed() ||
              !matches(tt, location_match_mode::kExact, fp.target(),
                       stp_transfer.get_location_idx())) {
            continue;
          }
          auto const arr =
              rtt != nullptr && rtt->resolve_rt(ree.r_.t_) !=
                                    rt_transport_idx_t::invalid()
                  ? rtt->unix_event_time(rtt->resolve_rt(ree.r_.t_), stop_idx,
                                         event_type::kArr)
                  : tt.event_time(ree.r_.t_, stop_idx, event_type::kArr);
          auto const arr_fp = arr + fp.duration();
          auto const dep =
              rtt != nullptr && rtt->resolve_rt(ree_transfer.r_.t_) !=
                                    rt_transport_idx_t::invalid()
                  ? rtt->unix_event_time(rtt->resolve_rt(ree_transfer.r_.t_),
                                         stop_idx_transfer, event_type::kDep)
                  : tt.event_time(ree_transfer.r_.t_, stop_idx_transfer,
                                  event_type::kDep);
          if (arr_fp <= dep) {
            leg.to_ = stp.get_location_idx();
            leg.arr_time_ = arr;
            ree.stop_range_.to_ = stop_idx + 1U;
            leg_footpath.from_ = stp.get_location_idx();
            leg_footpath.to_ = stp_transfer.get_location_idx();
            leg_footpath.dep_time_ = arr;
            leg_footpath.arr_time_ = arr_fp;
            leg_footpath.uses_ = fp;
            leg_transfer.from_ = stp_transfer.get_location_idx();
            leg_transfer.dep_time_ = dep;
            ree_transfer.stop_range_.from_ = stop_idx_transfer;
            fp_dur_best = fp.duration();
          }
          break;
        }
      }
    }
  }
}

template <direction SearchDir>
void optimize_footpaths(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q,
                        journey& j) {
  optimize_start<SearchDir>(tt, rtt, q, j);
  optimize_end<SearchDir>(tt, rtt, q, j);
  optimize_transfers(tt, rtt, q, j);
}

template void optimize_footpaths<direction::kForward>(timetable const& tt,
                                                      rt_timetable const* rtt,
                                                      query const& q,
                                                      journey& j);
template void optimize_footpaths<direction::kBackward>(timetable const& tt,
                                                       rt_timetable const* rtt,
                                                       query const& q,
                                                       journey& j);

}  // namespace nigiri::routing