#pragma once

#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/search.h"
#include "nigiri/types.h"

namespace nigiri {
struct timetable;
struct rt_timetable;
}  // namespace nigiri

namespace nigiri::routing {
struct query;

struct bidir_lb_raptor_stats {
  void reset() {
    pattern_reconstructions_ = 0UL;
    pattern_repetitions_ = 0UL;
    truncated_patterns_ = 0UL;
    unrealizable_patterns_ = 0UL;
    pruned_meetpoints_ = 0UL;
    duplicate_journeys_ = 0UL;
    passthrough_patterns_ = 0UL;
    dominated_continuations_ = 0UL;
    passthrough_journeys_ = 0UL;
    reboardings_ = 0UL;
    terminal_detours_ = 0UL;
    extra_passes_ = 0UL;
    rounds_ = 0UL;
    round_ms_ = 0UL;
  }

  std::uint64_t pattern_reconstructions_;
  std::uint64_t pattern_repetitions_;
  // reconstruct() did not reach the start/destination
  std::uint64_t truncated_patterns_;
  // no journey could be built for a complete pattern
  std::uint64_t unrealizable_patterns_;
  // dropped by `max_patterns_per_round_` before being reconstructed
  std::uint64_t pruned_meetpoints_;
  // realized, but same vehicles + same criteria as a journey already found
  std::uint64_t duplicate_journeys_;
  // contained a cycle or ran through a terminal and was cut back
  std::uint64_t passthrough_patterns_;
  // dropped: another journey shares its prefix and continues at least as well
  std::uint64_t dominated_continuations_;
  // dropped: rides through the destination and comes back to it
  std::uint64_t passthrough_journeys_;
  // dropped: gets off a line and boards the same line again at that stop
  std::uint64_t reboardings_;
  // dropped: walks through one terminal stop to board at another
  std::uint64_t terminal_detours_;
  // extra realization passes run to reach `min_connection_count_`
  std::uint64_t extra_passes_;
  // rounds executed (each one relaxes forward *and* backward)
  std::uint64_t rounds_;
  // milliseconds spent relaxing the lb routes, i.e. in `run()`
  std::uint64_t round_ms_;
};

struct bidir_lb_raptor {
  void execute(timetable const&,
               rt_timetable const*,
               query const&,
               bool arrive_by = false);

private:
  void reset(unsigned n_locations, unsigned n_lb_routes);

  template <direction SearchDir>
  void init(timetable const&, query const&, bool arrive_by);

  template <direction SearchDir>
  bool run(timetable const&, query const&, unsigned k);

  // Drops journeys that share a prefix with another journey - same vehicles,
  // same boarding and alighting stops - and continue from that point worse in
  // both arrival and transfers. Standing at the same station at the same time,
  // the better continuation is available to both, so the worse one carries no
  // information: it is not a different way *there*, only a worse way *on*.
  void drop_dominated_continuations();

  // Drops journeys that ride *through* the destination - not only stop at it as
  // a pattern node, but pass it inside a transit leg - and then come back to
  // it. Getting off there is the same journey with the tail cut, so it always
  // arrives earlier with fewer transfers.
  void drop_destination_passthrough(timetable const&);

  // Drops journeys that leave a route and board the same route again at that
  // very stop - waiting there for a later run of an identical stop sequence
  // (or, in the worst case, for the train just left). Staying on board is the
  // same journey minus the wait, and is realizable by construction.
  void drop_same_route_reboarding(timetable const&);

  // Drops journeys whose access (egress) walks lead through one stop of the
  // query's start (destination) and then to another one to board (alight)
  // there. The query covers that stop directly, so the detour is never needed -
  // it is an artifact of the pattern being anchored at the stop the search
  // happened to pick.
  void drop_terminal_detours(timetable const&);

  // One more departure for every pattern that still has a journey in the
  // result, anchored one minute past the departure realized last. Returns
  // false when no pattern yields anything any more.
  bool realize_next_departures(timetable const&,
                               rt_timetable const*,
                               query const&,
                               bool arrive_by);

  // Scores the meetpoints found in round `k` and appends them to
  // `scored_meetpoints_` - nothing is reconstructed yet, the budget is spent
  // once all rounds are done.
  template <direction SearchDir>
  void collect_meetpoints(unsigned k);

  // Takes the globally best `max_patterns_` meetpoints, reconstructs a pattern
  // for each and realizes it.
  void build_patterns(timetable const&,
                      rt_timetable const*,
                      query const&,
                      bool arrive_by);

  // Returns false if the terminal was not reached, i.e. the pattern in
  // `current_pattern_` is truncated and therefore unusable.
  template <direction SearchDir>
  [[nodiscard]] bool reconstruct(timetable const&,
                                 query const&,
                                 location_idx_t,
                                 unsigned k_start);

public:
  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      fwd_round_times_;
  std::array<vector_map<location_idx_t, std::uint16_t>,
             (kMaxTransfers + 2U) / 2U>
      bwd_round_times_;
  vector_map<location_idx_t, std::uint16_t> tmp_;
  bitvec fwd_station_mark_;
  bitvec bwd_station_mark_;
  bitvec prev_station_mark_;
  bitvec fwd_reached_;
  bitvec bwd_reached_;
  bitvec is_start_;
  bitvec is_dest_;
  bitvec lb_route_mark_;
  // Lower bound on what a journey through a meetpoint can achieve. Both parts
  // follow from the round tables alone, so meetpoints can be ranked *before*
  // anything is reconstructed: the forward search needs `fwd_k` transports to
  // reach the meetpoint and the backward search `bwd_k`, and the two labels
  // add up to the travel time through it.
  struct scored_meetpoint {
    friend bool operator<(scored_meetpoint const& a,
                          scored_meetpoint const& b) {
      return std::tie(a.transfers_, a.travel_time_lb_) <
             std::tie(b.transfers_, b.travel_time_lb_);
    }

    location_idx_t l_;
    std::uint8_t transfers_;
    std::uint32_t travel_time_lb_;
    // the rounds the two halves of the pattern are reconstructed from
    unsigned fwd_k_;
    unsigned bwd_k_;
  };

  // Meetpoints turned into patterns per *query*, best score first. The budget
  // is global rather than per round because the rounds are wildly uneven: the
  // first two produce nothing at all (nothing has met yet), one or two in the
  // middle produce hundreds, and the last ones taper off again. A per-round cap
  // therefore starves the rounds that matter while leaving the empty ones
  // unused. Set it to `unsigned` max for no limit.
  unsigned max_patterns_{20U};

  // Two journeys that agree on the transfer count and on every (transport,
  // boarding stop, alighting stop) are the same connection: only the access and
  // egress walks can still differ, and different pattern nodes inside one
  // station complex produce those by the dozen. They are collapsed here,
  // keeping the tightest variant. Where the passenger *changes* is part of the
  // key - that is a transfer pattern of its own and has to survive.
  using journey_key = std::vector<std::uint64_t>;
  static journey_key key_of(journey const&);
  // door to door, and the part of it that is walked - the tie-breakers between
  // two journeys that ride the same vehicles
  static duration_t span(journey const&);
  static duration_t walk_time(journey const&);
  // keeps `j` if nothing with its key is in the result yet, or if it uses the
  // same vehicles more tightly than what is
  void add_journey(journey&&);
  std::map<journey_key, std::size_t> journey_index_;
  std::vector<journey> new_journeys_;

  // Every pattern that produced a journey, in the order it was found (best
  // first), together with the anchor its next realization starts from. The
  // search window is not what limits the result - `min_connection_count_` is:
  // patterns are realized again and again, each time one minute past the
  // departure realized last, until enough journeys survive the filters.
  struct realizable {
    std::vector<location_idx_t> pattern_;
    unixtime_t next_anchor_;
  };
  std::vector<realizable> realizable_;
  // journey -> the pattern it came from, so the extra passes only touch
  // patterns that still have something in the result
  std::map<journey_key, std::size_t> pattern_of_;

  std::map<location_idx_t, std::uint16_t> min_;
  std::vector<location_idx_t> meetpoints_;
  std::vector<scored_meetpoint> scored_meetpoints_;
  std::vector<location_idx_t> current_pattern_;
  using pattern_t = std::array<location_idx_t, kMaxTransfers + 2U>;
  std::set<pattern_t> patterns_;

  // All alternatives, one per distinct pattern - deliberately *not* a
  // pareto_set: a journey that is dominated on (departure, arrival, transfers)
  // may still be interesting because of the pattern it uses.
  std::vector<journey> journeys_;
  bidir_lb_raptor_stats stats_;
};

// Entry point matching `raptor_search` / `pong_search`, so the algorithm can
// be selected like any other.
//
// Unlike those, the result is *not* a pareto front: every alternative found is
// handed on via `pareto_set::add_not_optimal`. A journey that is dominated on
// (departure, arrival, transfers) may still be interesting because of the
// transfer pattern it uses, which is the whole point of this algorithm.
routing_result bidir_lb_raptor_search(
    timetable const&,
    rt_timetable const*,
    search_state&,
    query,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing