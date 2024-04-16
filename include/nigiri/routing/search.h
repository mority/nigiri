#pragma once

#include "utl/enumerate.h"
#include "utl/equal_ranges_linear.h"
#include "utl/erase_if.h"
#include "utl/timing.h"
#include "utl/to_vec.h"

#include "nigiri/routing/dijkstra.h"
#include "nigiri/routing/for_each_meta.h"
#include "nigiri/routing/get_fastest_direct.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/start_times.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

struct search_state {
  search_state() = default;
  search_state(search_state const&) = delete;
  search_state& operator=(search_state const&) = delete;
  search_state(search_state&&) = default;
  search_state& operator=(search_state&&) = default;
  ~search_state() = default;

  std::vector<std::uint16_t> travel_time_lower_bound_;
  std::vector<bool> is_destination_;
  std::vector<std::uint16_t> dist_to_dest_;
  std::vector<start> starts_;
  pareto_set<journey> results_;
};

struct search_stats {
  std::uint64_t lb_time_{0ULL};
  std::uint64_t fastest_direct_{0ULL};
  std::uint64_t search_iterations_{0ULL};
  std::uint64_t interval_extensions_{0ULL};
  std::chrono::milliseconds execute_time_{0LL};  // [ms]
};

template <typename AlgoStats>
struct routing_result {
  pareto_set<journey> const* journeys_{nullptr};
  interval<unixtime_t> interval_;
  search_stats search_stats_;
  AlgoStats algo_stats_;
};

template <direction SearchDir, typename Algo>
struct search {
  using algo_state_t = typename Algo::algo_state_t;
  using algo_stats_t = typename Algo::algo_stats_t;
  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);

  Algo init(clasz_mask_t const allowed_claszes, algo_state_t& algo_state) {
    stats_.fastest_direct_ =
        static_cast<std::uint64_t>(fastest_direct_.count());

    collect_destinations(tt_, q_.destination_, q_.dest_match_mode_,
                         state_.is_destination_, state_.dist_to_dest_);

    if constexpr (Algo::kUseLowerBounds) {
      UTL_START_TIMING(lb);
      dijkstra(tt_, q_,
               kFwd ? tt_.fwd_search_lb_graph_ : tt_.bwd_search_lb_graph_,
               state_.travel_time_lower_bound_);
      for (auto i = 0U; i != tt_.n_locations(); ++i) {
        auto const lb = state_.travel_time_lower_bound_[i];
        for (auto const c : tt_.locations_.children_[location_idx_t{i}]) {
          state_.travel_time_lower_bound_[to_idx(c)] =
              std::min(lb, state_.travel_time_lower_bound_[to_idx(c)]);
        }
      }
      UTL_STOP_TIMING(lb);
      stats_.lb_time_ = static_cast<std::uint64_t>(UTL_TIMING_MS(lb));

#if defined(NIGIRI_TRACING)
      for (auto const& o : q_.start_) {
        trace_upd("start {}: {}\n", location{tt_, o.target()}, o.duration());
      }
      for (auto const& o : q_.destination_) {
        trace_upd("dest {}: {}\n", location{tt_, o.target()}, o.duration());
      }
      for (auto const [l, lb] :
           utl::enumerate(state_.travel_time_lower_bound_)) {
        if (lb != std::numeric_limits<std::decay_t<decltype(lb)>>::max()) {
          trace_upd("lb {}: {}\n", location{tt_, location_idx_t{l}}, lb);
        }
      }
#endif
    }

    return Algo{
        tt_,
        rtt_,
        algo_state,
        state_.is_destination_,
        state_.dist_to_dest_,
        state_.travel_time_lower_bound_,
        day_idx_t{std::chrono::duration_cast<date::days>(
                      search_interval_.from_ - tt_.internal_interval().from_)
                      .count()},
        allowed_claszes};
  }

  search(timetable const& tt,
         rt_timetable const* rtt,
         search_state& s,
         algo_state_t& algo_state,
         query q,
         std::optional<std::chrono::seconds> timeout = std::nullopt)
      : tt_{tt},
        rtt_{rtt},
        state_{s},
        q_{std::move(q)},
        search_interval_{std::visit(
            utl::overloaded{[](interval<unixtime_t> const start_interval) {
                              return start_interval;
                            },
                            [](unixtime_t const start_time) {
                              return interval<unixtime_t>{start_time,
                                                          start_time};
                            }},
            q_.start_time_)},
        fastest_direct_{get_fastest_direct(tt_, q_, SearchDir)},
        algo_{init(q_.allowed_claszes_, algo_state)},
        timeout_(timeout) {}

  routing_result<algo_stats_t> execute() {
    state_.results_.clear();

    if (start_dest_overlap()) {
      return {&state_.results_, search_interval_, stats_, algo_.get_stats()};
    }

    state_.starts_.clear();
    add_start_labels(q_.start_time_, true);

    auto const processing_start_time = std::chrono::steady_clock::now();
    auto const is_timeout_reached = [&]() {
      if (timeout_) {
        return (std::chrono::steady_clock::now() - processing_start_time) >=
               *timeout_;
      }

      return false;
    };

    while (true) {

      auto const interval_extension =
          n_results_in_interval() < q_.min_connection_count_
              ? estimate_interval_extension(q_.min_connection_count_ -
                                            n_results_in_interval())
              : std::tuple<duration_t, duration_t>{duration_t{0U},
                                                   duration_t{0U}};

      auto const new_interval = interval{
          q_.extend_interval_earlier_
              ? tt_.external_interval().clamp(search_interval_.from_ -
                                              std::get<0>(interval_extension))
              : search_interval_.from_,
          q_.extend_interval_later_
              ? tt_.external_interval().clamp(search_interval_.to_ +
                                              std::get<1>(interval_extension))
              : search_interval_.to_};
      trace("interval adapted: {} -> {}\n", search_interval_, new_interval);

      if (new_interval.from_ != search_interval_.from_) {
        add_start_labels(interval{new_interval.from_, search_interval_.from_},
                         kBwd);
        if constexpr (kBwd) {
          trace("dir=BWD, interval extension earlier -> reset state\n");
          algo_.reset_arrivals();
          remove_ontrip_results();
        }
      }

      if (new_interval.to_ != search_interval_.to_) {
        add_start_labels(interval{search_interval_.to_, new_interval.to_},
                         kFwd);
        if constexpr (kFwd) {
          trace("dir=BWD, interval extension later -> reset state\n");
          algo_.reset_arrivals();
          remove_ontrip_results();
        }
      }

      search_interval_ = new_interval;

      trace("start_time={}\n", search_interval_);

      search_interval();

      if (is_ontrip() || max_interval_reached() ||
          n_results_in_interval() >= q_.min_connection_count_ ||
          is_timeout_reached()) {
        trace(
            "  finished: is_ontrip={}, max_interval_reached={}, "
            "extend_earlier={}, extend_later={}, initial={}, interval={}, "
            "timetable={}, number_of_results_in_interval={}, "
            "timeout_reached={}\n",
            is_ontrip(), max_interval_reached(), q_.extend_interval_earlier_,
            q_.extend_interval_later_,
            std::visit(
                utl::overloaded{[](interval<unixtime_t> const& start_interval) {
                                  return start_interval;
                                },
                                [](unixtime_t const start_time) {
                                  return interval<unixtime_t>{start_time,
                                                              start_time};
                                }},
                q_.start_time_),
            search_interval_, tt_.external_interval(), n_results_in_interval(),
            is_timeout_reached());
        break;
      } else {
        trace(
            "  continue: max_interval_reached={}, extend_earlier={}, "
            "extend_later={}, initial={}, interval={}, timetable={}, "
            "number_of_results_in_interval={}\n",
            max_interval_reached(), q_.extend_interval_earlier_,
            q_.extend_interval_later_,
            std::visit(
                utl::overloaded{[](interval<unixtime_t> const& start_interval) {
                                  return start_interval;
                                },
                                [](unixtime_t const start_time) {
                                  return interval<unixtime_t>{start_time,
                                                              start_time};
                                }},
                q_.start_time_),
            search_interval_, tt_.external_interval(), n_results_in_interval());
      }

      state_.starts_.clear();
      ++stats_.search_iterations_;
    }

    if (is_pretrip()) {
      utl::erase_if(state_.results_, [&](journey const& j) {
        return !search_interval_.contains(j.start_time_) ||
               j.travel_time() >= fastest_direct_ ||
               j.travel_time() > kMaxTravelTime;
      });
      utl::sort(state_.results_, [](journey const& a, journey const& b) {
        return a.start_time_ < b.start_time_;
      });
    }

    stats_.execute_time_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::steady_clock::now() - processing_start_time));
    return {.journeys_ = &state_.results_,
            .interval_ = search_interval_,
            .search_stats_ = stats_,
            .algo_stats_ = algo_.get_stats()};
  }

private:
  bool is_ontrip() const {
    return holds_alternative<unixtime_t>(q_.start_time_);
  }

  bool is_pretrip() const { return !is_ontrip(); }

  bool start_dest_overlap() const {
    if (q_.start_match_mode_ == location_match_mode::kIntermodal ||
        q_.dest_match_mode_ == location_match_mode::kIntermodal) {
      return false;
    }

    auto const overlaps_start = [&](location_idx_t const x) {
      return utl::any_of(q_.start_, [&](offset const& o) {
        bool overlaps = false;
        for_each_meta(
            tt_, q_.start_match_mode_, o.target(),
            [&](location_idx_t const eq) { overlaps = overlaps || eq == x; });
        return overlaps;
      });
    };

    return utl::any_of(q_.destination_, [&](offset const& o) {
      auto overlaps = false;
      for_each_meta(tt_, q_.dest_match_mode_, o.target(),
                    [&](location_idx_t const eq) {
                      overlaps = overlaps || overlaps_start(eq);
                    });
      return overlaps;
    });
  }

  unsigned n_results_in_interval() const {
    if (holds_alternative<interval<unixtime_t>>(q_.start_time_)) {
      auto count = utl::count_if(state_.results_, [&](journey const& j) {
        return search_interval_.contains(j.start_time_);
      });
      return static_cast<unsigned>(count);
    } else {
      return static_cast<unsigned>(state_.results_.size());
    }
  }

  bool max_interval_reached() const {
    auto const can_search_earlier =
        q_.extend_interval_earlier_ &&
        search_interval_.from_ != tt_.external_interval().from_;
    auto const can_search_later =
        q_.extend_interval_later_ &&
        search_interval_.to_ != tt_.external_interval().to_;
    return !can_search_earlier && !can_search_later;
  }

  void add_start_labels(start_time_t const& start_interval,
                        bool const add_ontrip) {
    get_starts(SearchDir, tt_, rtt_, start_interval, q_.start_,
               q_.start_match_mode_, q_.use_start_footpaths_, state_.starts_,
               add_ontrip, q_.prf_idx_);
  }

  void remove_ontrip_results() {
    utl::erase_if(state_.results_, [&](journey const& j) {
      return !search_interval_.contains(j.start_time_);
    });
  }

  void search_interval() {
    utl::equal_ranges_linear(
        state_.starts_,
        [](start const& a, start const& b) {
          return a.time_at_start_ == b.time_at_start_;
        },
        [&](auto&& from_it, auto&& to_it) {
          algo_.next_start_time();
          auto const start_time = from_it->time_at_start_;
          for (auto const& s : it_range{from_it, to_it}) {
            trace("init: time_at_start={}, time_at_stop={} at {}\n",
                  s.time_at_start_, s.time_at_stop_, location_idx_t{s.stop_});
            algo_.add_start(s.stop_, s.time_at_stop_);
          }

          auto const worst_time_at_dest =
              start_time +
              (kFwd ? 1 : -1) * std::min(fastest_direct_, kMaxTravelTime);
          algo_.execute(start_time, q_.max_transfers_, worst_time_at_dest,
                        q_.prf_idx_, state_.results_);

          for (auto& j : state_.results_) {
            if (j.legs_.empty() &&
                (is_ontrip() || search_interval_.contains(j.start_time_)) &&
                j.travel_time() < fastest_direct_) {
              algo_.reconstruct(q_, j);
            }
          }
        });
  }

  float events_loc(day_idx_t day_idx, location_idx_t loc_idx) {
    auto n_transports = 0U;
    auto n_routes = 0U;

    auto const& routes = tt_.location_routes_[loc_idx];
    for (auto const route_idx : routes) {
      bool count_route = true;
      auto const transports = tt_.route_transport_ranges_[route_idx];
      for (auto const transport_idx : transports) {
        auto const bitfield_idx = tt_.transport_traffic_days_[transport_idx];
        auto const& bitfield = tt_.bitfields_[bitfield_idx];
        if (bitfield.test(day_idx.v_)) {
          ++n_transports;
          if (count_route) {
            ++n_routes;
            count_route = false;
          }
        }
      }
    }

    return n_routes == 0U ? 0.0f
                          : static_cast<float>(n_transports) /
                                static_cast<float>(n_routes);
  }

  float events_src_dest(day_idx_t day_idx) {
    // events at src
    float events_src = 0.0f;
    for (auto& o : q_.start_) {
      std::vector<char> name;
      for (auto c : tt_.locations_.names_.at(o.target())) {
        name.emplace_back(c);
      }
      std::cerr << "Adding events at source " << std::string_view(name) << "...\n";
      events_src += events_loc(day_idx, o.target());
      std::cerr << "events_src = " << events_src << "\n";
    }

    // events at destination
    float events_dest = 0.0f;
    for (auto u{0U}; u < state_.is_destination_.size(); ++u) {
      if (state_.is_destination_[u]) {
        std::vector<char> name;
        for (auto c : tt_.locations_.names_.at(location_idx_t{u})) {
          name.emplace_back(c);
        }
        std::cerr << "Adding events at destination " << std::string_view(name) << "...\n";
        events_dest += events_loc(day_idx, location_idx_t{u});
        std::cerr << "events_dest = " << events_dest << "\n";
      }
    }

    // return minimum
    return events_src < events_dest ? events_src : events_dest;
  }

  enum time_dir { earlier, later };

  duration_t estimate_time_dir(unsigned const num_con_req, time_dir dir) {
    float events = 0.0f;
    auto n_days = 0U;
    while (events < static_cast<float>(num_con_req)) {
      auto const new_interval_endpoint = dir == time_dir::earlier
                                             ? search_interval_.from_ - i32_minutes{n_days * 1440U}
                                             : search_interval_.to_ + i32_minutes{n_days * 1440U};

      if (!tt_.external_interval().contains(new_interval_endpoint)) {
        std::cerr << "end of timetable reached\n";
        return dir == time_dir::earlier
                   ? search_interval_.from_ - tt_.external_interval().from_
                   : tt_.external_interval().to_ - search_interval_.to_;
      }


      events += events_src_dest(
          day_idx_t{std::chrono::duration_cast<date::days>(
                        new_interval_endpoint - tt_.internal_interval().from_)
                        .count()});

      ++n_days;
    }
    return n_days == 1
               ? duration_t{static_cast<unsigned>(
                     (static_cast<float>(num_con_req) / events) * 1440.0f)}
               : duration_t{n_days * 1440U};
  }

  std::tuple<duration_t, duration_t> estimate_interval_extension(
      unsigned const num_con_req) {

    std::cerr << "estimate_earlier\n";
    auto estimate_earlier =
        q_.extend_interval_earlier_
            ? estimate_time_dir(num_con_req, time_dir::earlier)
            : duration_t{0U};

    std::cerr << "estimate_later\n";
    auto estimate_later = q_.extend_interval_later_
                              ? estimate_time_dir(num_con_req, time_dir::later)
                              : duration_t{0U};

    return {estimate_earlier, estimate_later};
  }

  timetable const& tt_;
  rt_timetable const* rtt_;
  search_state& state_;
  query q_;
  interval<unixtime_t> search_interval_;
  search_stats stats_;
  duration_t fastest_direct_;
  Algo algo_;
  std::optional<std::chrono::seconds> timeout_;
};

}  // namespace nigiri::routing
