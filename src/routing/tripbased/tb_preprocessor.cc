#include "utl/get_or_create.h"
#include "utl/progress_tracker.h"

#include "nigiri/logging.h"
#include "nigiri/routing/tripbased/expanded_transfer_set.h"
#include "nigiri/routing/tripbased/tb_preprocessor.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

#include <thread>

using namespace nigiri;
using namespace nigiri::routing::tripbased;

bitfield_idx_t tb_preprocessor::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

#ifdef TB_PREPRO_TRANSFER_REDUCTION
void tb_preprocessor::earliest_times::update(location_idx_t location,
                                             duration_t time_new,
                                             bitfield const& bf,
                                             bitfield* impr) {

  if (times_[location].size() != 0) {
    // bitfield is manipulated during update process
    bf_new_ = bf;
    // position of entry with an equal time
    auto same_time_spot = static_cast<unsigned>(times_[location].size());
    // position of entry with no active days
    auto overwrite_spot = static_cast<unsigned>(times_[location].size());
    // compare to existing entries of this location
    for (auto i{0U}; i != times_[location].size(); ++i) {
      if (bf_new_.none()) {
        // all bits of new entry were set to zero, new entry does not improve
        // upon any times
        return;
      } else if (times_[location][i].bf_.any()) {
        if (time_new < times_[location][i].time_) {
          // new time is better than existing time, update bit set of existing
          // time
          times_[location][i].bf_ &= ~bf_new_;
          if (times_[location][i].bf_.none()) {
            // existing entry has no active days left -> remember as overwrite
            // spot
            overwrite_spot = i;
          }
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
      } else {
        // existing entry has no active days -> remember as overwrite spot
        overwrite_spot = i;
      }
    }
    // after comparison to existing entries
    if (bf_new_.any()) {
      // new time has at least one active day after comparison
      if (same_time_spot != times_[location].size()) {
        // entry for this time already exists -> add active days of new time to
        // it
        times_[location][same_time_spot].bf_ |= bf_new_;
      } else if (overwrite_spot != times_[location].size()) {
        // overwrite spot was found -> use for new entry
        times_[location][overwrite_spot].time_ = time_new;
        times_[location][overwrite_spot].bf_ = bf_new_;
      } else {
        // no entry for this time exists yet
        times_[location].emplace_back(time_new, bf_new_);
      }
      // add improvements to impr
      if (impr != nullptr) {
        *impr |= bf_new_;
      }
    }
  } else {
    // add first entry for this location
    times_[location].emplace_back(time_new, bf);
    // improvement on all active days of the given bitfield
    if (impr != nullptr) {
      *impr |= bf;
    }
  }
}
#endif

void tb_preprocessor::build(transfer_set& ts) {

  // progress tracker
  //  auto progress_tracker = utl::get_active_progress_tracker();
  //  progress_tracker->status("Building Transfer Set")
  //      .reset_bounds()
  //      .in_high(tt_.transport_traffic_days_.size());

  auto num_threads = std::thread::hardware_concurrency();
  auto const num_transports = tt_.transport_traffic_days_.size();
  std::cerr << "num_threads = " << std::to_string(num_threads) << "\n";
  std::cerr << "num_transports = "
            << std::to_string(tt_.transport_traffic_days_.size()) << "\n";

  num_threads = num_threads > num_transports ? num_transports : num_threads;

  // part files for assembly
  std::vector<std::filesystem::path> part_files;
  part_files.reserve(num_threads);

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  auto const chunk_size = tt_.transport_traffic_days_.size() / num_threads;

  std::cerr << "chunk_size = " << std::to_string(chunk_size) << "\n";

  for (std::uint32_t thread_idx = 0U; thread_idx != num_threads; ++thread_idx) {
    part_files.emplace_back("ts_part." + std::to_string(thread_idx));
    auto const start = chunk_size * thread_idx;
    auto end = start + chunk_size;
    if (thread_idx + 1 == num_threads) {
      end = tt_.transport_traffic_days_.size();
    }
    threads.emplace_back(build_part, thread_idx, part_files[thread_idx], tt_,
                         start, end, transfer_time_max_, route_max_length_);
  }

  std::vector<std::vector<transfer>> transfers_per_transport;
  transfers_per_transport.resize(route_max_length_);

  for (std::uint32_t thread_idx = 0U; thread_idx != num_threads; ++thread_idx) {
    std::cerr << "Waiting for thread " << std::to_string(thread_idx)
              << " to finish\n";

    // wait for thread to finish
    threads[thread_idx].join();

    // load part file of thread
    cista::wrapped<expanded_transfer_set> ts_part =
        expanded_transfer_set::read(cista::memory_holder{
            cista::file{part_files[thread_idx].string().c_str(), "r"}
                .content()});

    // iterate transports in part file
    for (std::uint32_t t = 0U; t != ts_part->data_.size(); ++t) {

      // stop index
      std::uint64_t s = 0U;

      // deduplicate bitfields
      for (; s != ts_part->data_.size(t); ++s) {
        for (auto const& exp_transfer : ts_part->data_.at(t, s)) {
          transfers_per_transport[s].emplace_back(
              get_or_create_bfi(exp_transfer.bf_).v_,
              exp_transfer.transport_idx_to_.v_, exp_transfer.stop_idx_to_,
              exp_transfer.passes_midnight_);
          ++n_transfers_;
        }
      }

      // add transfers to transfer set
      ts.data_.emplace_back(it_range{
          transfers_per_transport.cbegin(),
          transfers_per_transport.cbegin() + static_cast<std::int64_t>(s)});

      // delete part file
      std::filesystem::remove(part_files[thread_idx]);

      // clean up helper vector
      for (std::uint64_t clean_s = 0U; clean_s != s; ++clean_s) {
        transfers_per_transport[clean_s].clear();
      }
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
}

void tb_preprocessor::build_part(std::uint32_t thread_idx,
                                 std::filesystem::path part_file,
                                 timetable const& tt_,
                                 std::uint32_t const start,
                                 std::uint32_t const end,
                                 duration_t const transfer_time_max_,
                                 std::uint32_t const route_max_length) {
  std::cerr << "thread " << thread_idx << ": processing transports "
            << std::to_string(start) << " to " << std::to_string(end - 1)
            << "\n";

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  earliest_times ets_arr_;
  earliest_times ets_ch_;
#endif

  // partial transfer set of expanded transfers
  expanded_transfer_set ts_part;

  std::vector<std::vector<expanded_transfer>> transfers_per_transport;
  transfers_per_transport.resize(route_max_length);

  // days of transport that still require a connection
  bitfield omega;

  // active days of current transfer
  bitfield theta;

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  // days on which the transfer constitutes an
  // improvement
  bitfield impr;
#endif

  // iterate over transports assigned to this thread
  for (transport_idx_t t{start}; t != end; ++t) {

    // route index of the current transport
    auto const route_t = tt_.transport_route_[t];

    // the stops of the current transport
    auto const stop_seq_t = tt_.route_location_seq_[route_t];

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

      // delta for tau_arr(t,i)
      auto const tau_arr_t_i_delta = tt_.event_mam(t, i, event_type::kArr);

      // time of day for tau_arr(t,i)
      auto const alpha = duration_t{tau_arr_t_i_delta.mam()};

      // shift amount due to travel time of the transport we are
      // transferring from
      auto const sigma_t = tau_arr_t_i_delta.days();

#ifdef TB_PREPRO_TRANSFER_REDUCTION
      // duration for tau_arr(t,i)
      auto const tau_arr_t_i = tau_arr_t_i_delta.as_duration();
      // the bitfield of the transport we are transferring from
      auto const& beta_t = tt_.bitfields_[tt_.transport_traffic_days_[t]];
      // init the earliest times data structure
      ets_arr_.update(p_t_i, tau_arr_t_i, beta_t, nullptr);
      for (auto const& fp : tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.update(fp.target(), tau_arr_t_i + fp.duration(), beta_t,
                        nullptr);
        ets_ch_.update(fp.target(), tau_arr_t_i + fp.duration(), beta_t,
                       nullptr);
      }
#endif

      auto handle_fp = [&sigma_t, &tt_, &t, &i, &route_t,
                        &transfers_per_transport, &alpha
#ifdef TB_PREPRO_TRANSFER_REDUCTION
                        ,
                        &tau_arr_t_i, &ets_arr_, &ets_ch_, &impr
#endif
                        ,
                        &omega, &theta,
                        &transfer_time_max_](footpath const& fp) {
        // q: location index of destination of footpath
        auto const q = fp.target();

        // arrival at stop q in alpha time scale
        auto const tau_q = delta{alpha + fp.duration()};

        // iterate over lines serving stop_to
        auto const routes_at_q = tt_.location_routes_[q];

        // ri_to: route index to
        for (auto const route_u : routes_at_q) {

          // route_u might visit stop multiple times, skip if stop_to is the
          // last stop in the stop sequence of route_u si_to: stop index to
          for (stop_idx_t j = 0U;
               j < tt_.route_location_seq_[route_u].size() - 1; ++j) {

            // location index at current stop index
            auto const p_u_j =
                stop{tt_.route_location_seq_[route_u][j]}.location_idx();

            // stop must match and entering must be allowed
            if (p_u_j ==
                q&& stop{tt_.route_location_seq_[route_u][j]}.in_allowed()) {

              // departure times of transports of route route_u at stop j
              auto const event_times =
                  tt_.event_times_at_stop(route_u, j, event_type::kDep);

              // find first departure at or after a
              // departure time of current transport_to
              auto tau_dep_u_j = std::lower_bound(
                  event_times.begin(), event_times.end(), tau_q,
                  [&](auto&& x, auto&& y) { return x.mam() < y.mam(); });

              // shift amount during transfer
              auto sigma = tau_q.days();

              // no departure on this day at or after a
              if (tau_dep_u_j == event_times.end()) {
                ++sigma;  // start looking on the following day
                tau_dep_u_j =
                    event_times.begin();  // with the earliest transport
              }

              // days of t that still require connection
              omega = tt_.bitfields_[tt_.transport_traffic_days_[t]];

              // check if any bit in omega is set to 1 and maximum waiting
              // time not exceeded
              while (omega.any()) {
                // init theta
                theta = omega;

                // departure time of current transport in relation to time
                // tau_alpha_delta
                auto const tau_dep_alpha_u_j =
                    duration_t{tau_dep_u_j->mam() + sigma * 1440};

                // check if max transfer time is exceeded
                if (tau_dep_alpha_u_j - alpha > transfer_time_max_) {
                  break;
                }

                // offset from begin of tp_to interval
                auto const k = std::distance(event_times.begin(), tau_dep_u_j);

                // transport index of transport that we transfer to
                auto const u =
                    tt_.route_transport_ranges_[route_u]
                                               [static_cast<std::size_t>(k)];

                // check conditions for required transfer
                // 1. different route OR
                // 2. earlier stop    OR
                // 3. same route but u is earlier than t
                auto const req =
                    route_t != route_u || j < i ||
                    (u != t &&
                     (tau_dep_alpha_u_j -
                          (tt_.event_mam(u, j, event_type::kDep).as_duration() -
                           tt_.event_mam(u, i, event_type::kArr)
                               .as_duration()) <
                      alpha));

                if (req) {
                  // shift amount due to number of times transport u passed
                  // midnight
                  auto const sigma_u = tau_dep_u_j->days();

                  // total shift amount
                  auto const sigma_total = sigma_u - sigma_t - sigma;

                  // bitfield transport to
                  auto const& beta_u =
                      tt_.bitfields_[tt_.transport_traffic_days_[u]];

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
                                              &t, &tt_]() {
                      // check if next stop for u and previous stop for
                      // t exists
                      if (j + 1 < tt_.route_location_seq_[route_u].size() &&
                          i - 1 > 0) {
                        // check if next stop of u is the previous stop
                        // of t
                        auto const p_u_next =
                            stop{tt_.route_location_seq_[route_u][j + 1]};
                        auto const p_t_prev =
                            stop{tt_.route_location_seq_[route_t][i - 1]};
                        if (p_u_next.location_idx() ==
                            p_t_prev.location_idx()) {
                          // check if u is already reachable at the
                          // previous stop of t
                          auto const tau_dep_alpha_u_next =
                              tau_dep_alpha_u_j +
                              (tt_.event_mam(u, j + 1, event_type::kDep)
                                   .as_duration() -
                               tt_.event_mam(u, j, event_type::kDep)
                                   .as_duration());
                          auto const tau_arr_alpha_t_prev =
                              alpha - (tt_.event_mam(t, i, event_type::kArr)
                                           .as_duration() -
                                       tt_.event_mam(t, i, event_type::kArr)
                                           .as_duration());
                          auto const min_change_time =
                              tt_.locations_
                                  .transfer_time_[p_t_prev.location_idx()];
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
                           l != tt_.route_location_seq_[route_u].size(); ++l) {
                        auto const tau_arr_t_u_l =
                            tau_dep_t_u_j +
                            (tt_.event_mam(u, l, event_type::kArr)
                                 .as_duration() -
                             tau_dep_u_j->as_duration());
                        auto const p_u_l =
                            stop{tt_.route_location_seq_[route_u][l]}
                                .location_idx();
                        ets_arr_.update(p_u_l, tau_arr_t_u_l, theta, &impr);
                        for (auto const& fp_r :
                             tt_.locations_.footpaths_out_[p_u_l]) {
                          auto const eta = tau_arr_t_u_l + fp_r.duration();
                          ets_arr_.update(fp_r.target(), eta, theta, &impr);
                          ets_ch_.update(fp_r.target(), eta, theta, &impr);
                        }
                      }

                      std::swap(theta, impr);
                      if (theta.any()) {
#endif
                        // add transfer to transfers of this transport
                        transfers_per_transport[i].emplace_back(theta, u, j,
                                                                sigma);

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
      handle_fp(footpath{p_t_i, tt_.locations_.transfer_time_[p_t_i]});

      // outgoing footpaths of location
      for (auto const& fp_q : tt_.locations_.footpaths_out_[p_t_i]) {
        handle_fp(fp_q);
      }
    }

    // add transfers of this transport to the transfer set
    ts_part.data_.emplace_back(
        it_range{transfers_per_transport.cbegin(),
                 transfers_per_transport.cbegin() +
                     static_cast<std::int64_t>(stop_seq_t.size())});

    // clean up helper vector
    for (std::uint32_t s = 0U; s != stop_seq_t.size(); ++s) {
      transfers_per_transport[s].clear();
    }
  }

  // write found transfers to part file
  ts_part.write(part_file);
}
