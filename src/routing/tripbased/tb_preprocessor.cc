#include "utl/get_or_create.h"
#include "utl/progress_tracker.h"

#include "nigiri/logging.h"
#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

#include <chrono>
#include <mutex>
#include <thread>

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace std::chrono_literals;

bitfield_idx_t tb_preprocessor::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

#ifdef TB_PREPRO_TRANSFER_REDUCTION
void tb_preprocessor::earliest_times::init(location_idx_t location,
                                           std::int32_t time_new,
                                           bitfield const& bf) {
  times_[location].emplace_back(time_new, bf);
}

void tb_preprocessor::earliest_times::update(location_idx_t location,
                                             std::int32_t time_new,
                                             bitfield const& bf,
                                             bitfield& impr) {
  // bitfield is manipulated during update process
  bf_new_ = bf;
  // position of entry with an equal time
  std::optional<std::uint32_t> same_time_spot = std::nullopt;
  // position of entry with no active days
  std::optional<std::uint32_t> overwrite_spot = std::nullopt;
  // compare to existing entries of this location
  for (auto i{0U}; i != times_[location].size(); ++i) {
    if (bf_new_.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return;
    }
    if (time_new < times_[location][i].time_) {
      // new time is better than existing time, update bit set of existing
      // time
      times_[location][i].bf_ &= ~bf_new_;
    } else {
      // new time is greater or equal
      // remove active days from new time that are already active in the
      // existing entry
      bf_new_ &= ~times_[location][i].bf_;
      if (time_new == times_[location][i].time_) {
        // remember this position to add active days of new time after
        // comparison to existing entries
        same_time_spot = i;
      }
    }
    if (times_[location][i].bf_.none()) {
      // existing entry has no active days left -> remember as overwrite
      // spot
      overwrite_spot = i;
    }
  }
  // after comparison to existing entries
  if (bf_new_.any()) {
    // new time has at least one active day after comparison
    if (same_time_spot.has_value()) {
      // entry for this time already exists -> add active days of new time to
      // it
      times_[location][same_time_spot.value()].bf_ |= bf_new_;
    } else if (overwrite_spot.has_value()) {
      // overwrite spot was found -> use for new entry
      times_[location][overwrite_spot.value()].time_ = time_new;
      times_[location][overwrite_spot.value()].bf_ = bf_new_;
    } else {
      // add new entry
      times_[location].emplace_back(time_new, bf_new_);
    }
    // add improvements to impr
    impr |= bf_new_;
  }
}
#endif

void tb_preprocessor::build(transfer_set& ts) {
  auto const num_transports = tt_.transport_traffic_days_.size();
  auto const num_threads = std::thread::hardware_concurrency();

  // progress tracker
  auto progress_tracker = utl::get_active_progress_tracker();
  progress_tracker->status("Building Transfer Set")
      .reset_bounds()
      .in_high(num_transports);

  std::vector<std::thread> threads;

  // start worker threads
  for (unsigned i = 0; i != num_threads; ++i) {
    threads.emplace_back(build_part, this);
  }

  std::vector<std::vector<transfer>> transfers_per_transport;
  transfers_per_transport.resize(route_max_length_);

  // next transport idx for which to deduplicate bitfields
  std::uint32_t next_deduplicate = 0U;
  while (next_deduplicate != num_transports) {
    std::this_thread::sleep_for(1000ms);

    // check if next part is ready
    for (auto part = parts_.begin(); part != parts_.end();) {
      if (part->first == next_deduplicate) {

        // deduplicate
        for (unsigned s = 0U; s != part->second.size(); ++s) {
          for (auto const& exp_transfer : part->second[s]) {
            transfers_per_transport[s].emplace_back(
                get_or_create_bfi(exp_transfer.bf_).v_,
                exp_transfer.transport_idx_to_.v_, exp_transfer.stop_idx_to_,
                exp_transfer.passes_midnight_);
            ++n_transfers_;
          }
        }
        // add transfers of this transport to transfer set
        ts.data_.emplace_back(
            it_range{transfers_per_transport.cbegin(),
                     transfers_per_transport.cbegin() +
                         static_cast<std::int64_t>(part->second.size())});
        // clean up
        for (unsigned s = 0U; s != part->second.size(); ++s) {
          transfers_per_transport[s].clear();
        }

        ++next_deduplicate;
        progress_tracker->increment();

        // remove processed part
        std::lock_guard<std::mutex> const lock(parts_mutex_);
        parts_.erase(part);
        // start from begin, next part maybe more towards the front of the queue
        part = parts_.begin();
      } else {
        ++part;
      }
    }
  }

  // join worker threads
  for (auto& t : threads) {
    t.join();
  }

  std::cout << "Found " << n_transfers_ << " transfers, occupying "
            << n_transfers_ * sizeof(transfer) << " bytes" << std::endl;

  ts.tt_hash_ = hash_tt(tt_);
  ts.num_el_con_ = num_el_con_;
  ts.route_max_length_ = route_max_length_;
  ts.transfer_time_max_ = transfer_time_max_;
  ts.n_transfers_ = n_transfers_;
  ts.ready_ = true;
}

void tb_preprocessor::build_part(tb_preprocessor* const pp) {

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  earliest_times ets_arr_;
  earliest_times ets_ch_;
#endif

  // days of transport that still require a connection
  bitfield omega;

  // active days of current transfer
  bitfield theta;

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  // days on which the transfer constitutes an
  // improvement
  bitfield impr;
#endif

  while (true) {

    std::uint32_t t_uint;

    // get next transport index to process
    {
      std::lock_guard<std::mutex> const lock(pp->next_transport_mutex_);
      t_uint = pp->next_transport_;
      if (t_uint == pp->tt_.transport_traffic_days_.size()) {
        break;
      }
      ++pp->next_transport_;
    }

    // init a new part
    part_t part;
    part.first = t_uint;

    transport_idx_t t{t_uint};

    // route index of the current transport
    auto const route_t = pp->tt_.transport_route_[t];

    // the stops of the current transport
    auto const stop_seq_t = pp->tt_.route_location_seq_[route_t];

    // resize for number of stops of this transport
    part.second.resize(stop_seq_t.size());

#ifdef TB_PREPRO_TRANSFER_REDUCTION
    // clear earliest times
    ets_arr_.reset();
    ets_ch_.reset();
    // reverse iteration
    for (auto i = static_cast<stop_idx_t>(stop_seq_t.size() - 1U); i != 0U;
         --i) {
#else
    for (auto i = static_cast<stop_idx_t>(1U); i != stop_seq_t.size(); ++i) {
#endif
      // skip stop if exiting is not allowed
      if (!stop{stop_seq_t[i]}.out_allowed()) {
        continue;
      }

      // the location index from which we are transferring
      auto const p_t_i = stop{stop_seq_t[i]}.location_idx();

      // tau_arr(t,i)
      std::int32_t const tau_arr_t_i =
          pp->tt_.event_mam(t, i, event_type::kArr).count();

      // time of day for tau_arr(t,i)
      std::int32_t const alpha = pp->tt_.event_mam(t, i, event_type::kArr).mam_;

      // shift amount due to travel time of the transport we are
      // transferring from
      std::int32_t const sigma_t =
          pp->tt_.event_mam(t, i, event_type::kArr).days_;

      // the bitfield of the transport we are transferring from
      auto const& beta_t =
          pp->tt_.bitfields_[pp->tt_.transport_traffic_days_[t]];

#ifdef TB_PREPRO_TRANSFER_REDUCTION
      // init the earliest times data structure
      ets_arr_.init(p_t_i, tau_arr_t_i, beta_t);
      for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.init(fp.target(), tau_arr_t_i + fp.duration().count(), beta_t);
        ets_ch_.init(fp.target(), tau_arr_t_i + fp.duration().count(), beta_t);
      }
#endif

      auto handle_fp = [&sigma_t, &pp, &t, &i, &route_t, &alpha
#ifdef TB_PREPRO_TRANSFER_REDUCTION
                        ,
                        &tau_arr_t_i, &ets_arr_, &ets_ch_, &impr
#endif
                        ,
                        &omega, &theta, &part, &beta_t](footpath const& fp) {
        // q: location index of destination of footpath
        auto const q = fp.target();

        // arrival at stop q in alpha time scale
        auto const tau_q = alpha + fp.duration().count();
        auto const tau_q_tod = tau_q % 1440;
        auto const tau_q_d = tau_q / 1440;

        // iterate over lines serving stop_to
        auto const routes_at_q = pp->tt_.location_routes_[q];

        // ri_to: route index to
        for (auto const route_u : routes_at_q) {

          // route_u might visit stop multiple times, skip if stop_to is the
          // last stop in the stop sequence of route_u si_to: stop index to
          for (stop_idx_t j = 0U;
               j < pp->tt_.route_location_seq_[route_u].size() - 1; ++j) {

            // location index at current stop index
            auto const p_u_j =
                stop{pp->tt_.route_location_seq_[route_u][j]}.location_idx();

            // stop must match and entering must be allowed
            if ((p_u_j == q) &&
                stop{pp->tt_.route_location_seq_[route_u][j]}.in_allowed()) {

              // departure times of transports of route route_u at stop j
              auto const event_times =
                  pp->tt_.event_times_at_stop(route_u, j, event_type::kDep);

              // find first departure at or after a
              // departure time of current transport_to
              auto tau_dep_u_j = std::lower_bound(
                  event_times.begin(), event_times.end(), tau_q_tod,
                  [&](auto&& x, auto&& y) { return x.mam_ < y; });

              // shift amount during transfer
              auto sigma = tau_q_d;

              // no departure on this day at or after a
              if (tau_dep_u_j == event_times.end()) {
                ++sigma;  // start looking on the following day
                tau_dep_u_j =
                    event_times.begin();  // with the earliest transport
              }

              // days of t that still require connection
              omega = beta_t;

              // check if any bit in omega is set to 1 and maximum waiting
              // time not exceeded
              while (omega.any()) {
                // init theta
                theta = omega;

                // departure time of current transport in relation to time
                // alpha
                auto const tau_dep_alpha_u_j = sigma * 1440 + tau_dep_u_j->mam_;

                // check if max transfer time is exceeded
                if (tau_dep_alpha_u_j - alpha > pp->transfer_time_max_) {
                  break;
                }

                // offset from begin of tp_to interval
                auto const k = std::distance(event_times.begin(), tau_dep_u_j);

                // transport index of transport that we transfer to
                auto const u =
                    pp->tt_
                        .route_transport_ranges_[route_u]
                                                [static_cast<std::size_t>(k)];

                // check conditions for required transfer
                // 1. different route OR
                // 2. earlier stop    OR
                // 3. same route but u is earlier than t
                auto const req =
                    route_t != route_u || j < i ||
                    (u != t &&
                     (tau_dep_alpha_u_j -
                          (pp->tt_.event_mam(u, j, event_type::kDep).count() -
                           pp->tt_.event_mam(u, i, event_type::kArr).count()) <
                      alpha));

                if (req) {
                  // shift amount due to number of times transport u passed
                  // midnight
                  auto const sigma_u = tau_dep_u_j->days();

                  // total shift amount
                  auto const sigma_total = sigma_u - sigma_t - sigma;

                  // bitfield transport to
                  auto const& beta_u =
                      pp->tt_.bitfields_[pp->tt_.transport_traffic_days_[u]];

                  // align bitfields and perform AND
                  if (sigma_total < 0) {
                    theta &= beta_u >> static_cast<unsigned>(-1 * sigma_total);
                  } else {
                    theta &= beta_u << static_cast<unsigned>(sigma_total);
                  }

                  // check for match
                  if (theta.any()) {

                    // remove days that are covered by this transfer from
                    // omega
                    omega &= ~theta;

#ifdef TB_PREPRO_UTURN_REMOVAL
                    auto const check_uturn = [&j, &route_u, &i, &route_t,
                                              &tau_dep_alpha_u_j, &u, &alpha,
                                              &t, &pp]() {
                      // check if next stop for u and previous stop for
                      // t exists
                      if (j + 1 < pp->tt_.route_location_seq_[route_u].size() &&
                          i - 1 > 0) {
                        // check if next stop of u is the previous stop
                        // of t
                        auto const p_u_next =
                            stop{pp->tt_.route_location_seq_[route_u][j + 1]};
                        auto const p_t_prev =
                            stop{pp->tt_.route_location_seq_[route_t][i - 1]};
                        if (p_u_next.location_idx() ==
                            p_t_prev.location_idx()) {
                          // check if u is already reachable at the
                          // previous stop of t
                          auto const tau_dep_alpha_u_next =
                              tau_dep_alpha_u_j +
                              (pp->tt_.event_mam(u, j + 1, event_type::kDep)
                                   .count() -
                               pp->tt_.event_mam(u, j, event_type::kDep)
                                   .count());
                          auto const tau_arr_alpha_t_prev =
                              alpha -
                              (pp->tt_.event_mam(t, i, event_type::kArr)
                                   .count() -
                               pp->tt_.event_mam(t, i - 1, event_type::kArr)
                                   .count());
                          auto const min_change_time =
                              pp->tt_.locations_
                                  .transfer_time_[p_t_prev.location_idx()]
                                  .count();
                          return tau_arr_alpha_t_prev + min_change_time <=
                                 tau_dep_alpha_u_next;
                        }
                      }
                      return false;
                    };
                    if (!check_uturn()) {
#endif
#ifdef TB_PREPRO_TRANSFER_REDUCTION
                      impr.reset();
                      auto const tau_dep_t_u_j =
                          tau_arr_t_i + (tau_dep_alpha_u_j - alpha);
                      for (stop_idx_t l = j + 1U;
                           l != pp->tt_.route_location_seq_[route_u].size();
                           ++l) {
                        auto const tau_arr_t_u_l =
                            tau_dep_t_u_j +
                            (pp->tt_.event_mam(u, l, event_type::kArr).count() -
                             tau_dep_u_j->count());
                        auto const p_u_l =
                            stop{pp->tt_.route_location_seq_[route_u][l]}
                                .location_idx();
                        ets_arr_.update(p_u_l, tau_arr_t_u_l, theta, impr);
                        for (auto const& fp_r :
                             pp->tt_.locations_.footpaths_out_[p_u_l]) {
                          auto const eta =
                              tau_arr_t_u_l + fp_r.duration().count();
                          ets_arr_.update(fp_r.target(), eta, theta, impr);
                          ets_ch_.update(fp_r.target(), eta, theta, impr);
                        }
                      }

                      std::swap(theta, impr);
                      if (theta.any()) {
#endif
                        // add transfer to transfers of this transport
                        part.second[i].emplace_back(theta, u, j, sigma);

#ifdef TB_PREPRO_TRANSFER_REDUCTION
                      }
#endif
#ifdef TB_PREPRO_UTURN_REMOVAL
                    }
#endif
                  }
                }

                // prep next iteration
                // is this the last transport of the day?
                if (std::next(tau_dep_u_j) == event_times.end()) {

                  // passing midnight
                  ++sigma;

                  // start with the earliest transport on the next day
                  tau_dep_u_j = event_times.begin();
                } else {
                  ++tau_dep_u_j;
                }
              }
            }
          }
        }
      };

      // virtual reflexive footpath
      handle_fp(footpath{p_t_i, pp->tt_.locations_.transfer_time_[p_t_i]});

      // outgoing footpaths of location
      for (auto const& fp_q : pp->tt_.locations_.footpaths_out_[p_t_i]) {
        handle_fp(fp_q);
      }
    }

    // add part to queue
    {
      std::lock_guard<std::mutex> const lock(pp->parts_mutex_);
      pp->parts_.push_back(std::move(part));
    }
  }
}
