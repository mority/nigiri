

#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/tb_preprocessing.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

bitfield_idx_t tb_preprocessing::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

#ifdef TB_PREPRO_TRANSFER_REDUCTION
bool tb_preprocessing::earliest_times::update(location_idx_t location_idx,
                                              duration_t time_new,
                                              bitfield const& bf) {
  // bitfield of the update
  bitfield bf_new = bf;

  // iterator to first entry that has no active days, overwrite with new entry
  // instead
  unsigned overwrite_spot =
      static_cast<unsigned>(location_idx_times_[location_idx].size());

  // iterate entries for this location
  for (auto i{0U}; i < location_idx_times_[location_idx].size(); ++i) {
    // the earliest time entry we are currently examining
    auto& et_cur = location_idx_times_[location_idx][i];
    if (bf_new.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return false;
    } else if (tbp_.tt_.bitfields_[et_cur.bf_idx_].any()) {
      if (time_new < et_cur.time_) {
        // new time is better than current time, update bit set of current
        // time
        et_cur.bf_idx_ = tbp_.get_or_create_bfi(
            tbp_.tt_.bitfields_[et_cur.bf_idx_] & ~bf_new);
      } else if (time_new == et_cur.time_) {
        // entry for this time already exists
        if ((bf_new & ~tbp_.tt_.bitfields_[et_cur.bf_idx_]).none()) {
          // all bits of bf_new already covered
          return false;
        } else {
          // new entry absorbs old entry
          bf_new |= tbp_.tt_.bitfields_[et_cur.bf_idx_];
          et_cur.bf_idx_ = tbp_.get_or_create_bfi(bitfield{"0"});
        }
      } else {
        // new time is worse than current time, update bit set of new time
        bf_new &= ~tbp_.tt_.bitfields_[et_cur.bf_idx_];
      }
    }
    // we remember the first invalid entry as an overwrite spot for the new
    // entry
    if (overwrite_spot == location_idx_times_[location_idx].size() &&
        tbp_.tt_.bitfields_[et_cur.bf_idx_].none()) {
      overwrite_spot = i;
    }
  }

  if (bf_new.any()) {
    if (overwrite_spot == location_idx_times_[location_idx].size()) {
      location_idx_times_[location_idx].emplace_back(
          time_new, tbp_.get_or_create_bfi(bf_new));
    } else {
      location_idx_times_[location_idx][overwrite_spot].time_ = time_new;
      location_idx_times_[location_idx][overwrite_spot].bf_idx_ =
          tbp_.get_or_create_bfi(bf_new);
    }
    return true;
  } else {
    return false;
  }
}
#endif

void tb_preprocessing::build_transfer_set() {

  // the transfers per stop of the transport currently examined
  std::vector<std::vector<transfer>> transfers;
  transfers.resize(route_max_length);
  for (auto& inner_vec : transfers) {
    inner_vec.reserve(64);
  }

  // iterate over all trips of the timetable
  for (transport_idx_t t{0U}; t != tt_.transport_traffic_days_.size(); ++t) {

    // route index of the current transport
    auto const route_t = tt_.transport_route_[t];

    // the stops of the current transport
    auto const stop_seq_t = tt_.route_location_seq_[route_t];

    // only clear the inner vector of transfers to prevent reallocation
    // only clear what we will be using during this iteration
    for (auto pos = 1U; pos < stop_seq_t.size(); ++pos) {
      transfers[pos].clear();
    }

#ifdef TB_PREPRO_TRANSFER_REDUCTION
    // clear earliest times
    ets_arr_.clear();
    ets_ch_.clear();
    // reverse iteration
    for (auto i = stop_seq_t.size() - 1; i != 0U; --i) {
#else
    for (auto i = 1U; i != stop_seq_t.size(); ++i) {
#endif

      // the location index from which we are transferring
      auto const p_t_i = stop{stop_seq_t[i]}.location_idx();

      // delta at from stop
      auto const tau_arr_t_i_delta = tt_.event_mam(t, i, event_type::kArr);

      // duration at from stop
      auto const tau_arr_t_i = tau_arr_t_i_delta.as_duration();

      // shift amount due to travel time of the transport we are transferring
      // from
      auto const sigma_t = day_idx_t{tau_arr_t_i_delta.days()};

#ifdef TB_PREPRO_TRANSFER_REDUCTION
      // the bitfield of the transport we are transferring from
      auto const& beta_t = tt_.bitfields_[tt_.transport_traffic_days_[t]];
      // init the earliest times data structure
      ets_arr_.update(p_t_i, tau_arr_t_i, beta_t);
      for (auto const& fp : tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.update(fp.target(), tau_arr_t_i + fp.duration(), beta_t);
        ets_ch_.update(fp.target(), tau_arr_t_i + fp.duration(), beta_t);
      }
#endif

      auto handle_fp = [&tau_arr_t_i, &sigma_t, this, &t, &i, &route_t,
                        &transfers](footpath const& fp) {
        // q: location index of destination of footpath
        auto const q = fp.target();

        auto const tau_alpha = tau_arr_t_i + fp.duration();

        // tau_alpha_delta: arrival time at stop_to in relation to t
        auto const tau_alpha_delta = delta{tau_alpha};

        // time of day when arriving a stop q
        auto const alpha = duration_t{tau_alpha_delta.mam()};

        // sigma_fp: shift amount footpath
        auto const sigma_fp = day_idx_t{tau_alpha_delta.days()} - sigma_t;

        // iterate over lines serving stop_to
        auto const routes_at_q = tt_.location_routes_[q];

        // ri_to: route index to
        for (auto const route_u : routes_at_q) {

          // route_u might visit stop multiple times, skip if stop_to is the
          // last stop in the stop sequence of route_u si_to: stop index to
          for (unsigned j = 0U; j < tt_.route_location_seq_[route_u].size() - 1;
               ++j) {

            // location index at current stop index
            auto const p_u_j =
                stop{tt_.route_location_seq_[route_u][j]}.location_idx();

            if (p_u_j == q) {

              // sigma_w: shift amount due to waiting for connection
              day_idx_t sigma_w{0U};

              // departure times of transports of route route_u at stop j
              auto const event_times =
                  tt_.event_times_at_stop(route_u, j, event_type::kDep);

              // find first departure at or after a
              // departure time of current transport_to
              auto tau_dep_u_j_delta = std::lower_bound(
                  event_times.begin(), event_times.end(), tau_alpha_delta,
                  [&](auto&& x, auto&& y) { return x.mam() < y.mam(); });

              // no departure on this day at or after a
              if (tau_dep_u_j_delta == event_times.end()) {
                ++sigma_w;  // start looking on the following day
                tau_dep_u_j_delta =
                    event_times.begin();  // with the earliest transport
              }

              // omega: days of t that still require connection
              bitfield omega = tt_.bitfields_[tt_.transport_traffic_days_[t]];

              // check if any bit in omega is set to 1 and maximum waiting
              // time not exceeded
              while (omega.any() && sigma_w <= sigma_w_max_) {

                // departure time of current transport in relation to time
                // tau_alpha_delta
                auto const tau_dep_alpha_u_j =
                    duration_t{tau_dep_u_j_delta->mam()} +
                    duration_t{sigma_w.v_ * 1440};

                // offset from begin of tp_to interval
                auto const k =
                    std::distance(event_times.begin(), tau_dep_u_j_delta);

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
                               .as_duration()) <=
                      alpha - fp.duration()));

                if (req) {
                  // shift amount due to number of times transport_to passed
                  // midnight
                  auto const sigma_u = day_idx_t{tau_dep_u_j_delta->days()};

                  // total shift amount
                  auto const sigma =
                      static_cast<int>(sigma_u.v_) -
                      static_cast<int>(sigma_t.v_ + sigma_fp.v_ + sigma_w.v_);

                  // bitfield transport to
                  auto const& beta_u =
                      tt_.bitfields_[tt_.transport_traffic_days_[u]];

                  // bitfield transfer from
                  auto theta = omega;

                  // align bitfields and perform AND
                  if (sigma < 0) {
                    theta &= beta_u >> static_cast<unsigned>(-1 * sigma);
                  } else {
                    theta &= beta_u << static_cast<unsigned>(sigma);
                  }

                  // check for match
                  if (theta.any()) {

                    // remove days that are covered by this transfer from
                    // omega
                    omega &= ~theta;

#ifdef TB_PREPRO_UTURN_REMOVAL
                    auto const check_uturn = [&j, this, &route_u, &i, &route_t,
                                              &tau_dep_alpha_u_j, &u, &alpha,
                                              &fp, &t]() {
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
                              alpha - fp.duration() -
                              (tt_.event_mam(t, i, event_type::kArr)
                                   .as_duration() -
                               tt_.event_mam(t, i, event_type::kArr)
                                   .as_duration());
                          auto const transfer_time =
                              tt_.locations_
                                  .transfer_time_[p_t_prev.location_idx()];
                          return tau_arr_alpha_t_prev + transfer_time <=
                                 tau_dep_alpha_u_next;
                        }
                      }
                      return false;
                    };
                    if (!check_uturn()) {
#endif
#ifdef TB_PREPRO_TRANSFER_REDUCTION
                      auto const check_impr = [&tau_alpha_delta, &alpha,
                                               &tau_dep_alpha_u_j, &j, this,
                                               &route_u, &u, &tau_dep_u_j_delta,
                                               &theta]() {
                        bool impr = false;
                        auto const tau_dep_t_u_j =
                            tau_alpha_delta.as_duration() +
                            (tau_dep_alpha_u_j - alpha);
                        for (auto l = j + 1;
                             l != tt_.route_location_seq_[route_u].size();
                             ++l) {
                          auto const tau_arr_t_u_l =
                              tau_dep_t_u_j +
                              (tt_.event_mam(u, l, event_type::kArr)
                                   .as_duration() -
                               tau_dep_u_j_delta->as_duration());
                          auto const p_u_l =
                              stop{tt_.route_location_seq_[route_u][l]}
                                  .location_idx();
                          auto const ets_arr_res =
                              ets_arr_.update(p_u_l, tau_arr_t_u_l, theta);
                          impr = impr || ets_arr_res;
                          for (auto const& fp_r :
                               tt_.locations_.footpaths_out_[p_u_l]) {
                            auto const eta = tau_arr_t_u_l + fp_r.duration();
                            auto const ets_arr_fp_res =
                                ets_arr_.update(fp_r.target(), eta, theta);
                            auto const ets_ch_fp_res =
                                ets_ch_.update(fp_r.target(), eta, theta);
                            impr = impr || ets_arr_fp_res || ets_ch_fp_res;
                          }
                        }
                        return impr;
                      };
                      if (check_impr()) {
#endif
                        // the bitfield index of the bitfield of the transfer
                        auto const theta_idx = get_or_create_bfi(theta);
                        // add transfer to transfers of this transport
                        transfers[i].emplace_back(transfer{
                            theta_idx.v_, u.v_, j, (sigma_fp + sigma_w).v_});
                        ++n_transfers_;
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
                if (std::next(tau_dep_u_j_delta) == event_times.end()) {

                  // passing midnight
                  ++sigma_w;

                  // start with the earliest transport on the next day
                  tau_dep_u_j_delta = event_times.begin();
                } else {
                  ++tau_dep_u_j_delta;
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
    ts_.emplace_back(it_range{
        transfers.cbegin(),
        transfers.cbegin() + static_cast<std::int64_t>(stop_seq_t.size())});
  }

  std::cout << "Found " << n_transfers_ << " transfers, occupying "
            << n_transfers_ * sizeof(transfer) << " bytes" << std::endl;
  ts_ready_ = true;
}

auto tb_preprocessing::file_names(
    std::filesystem::path const& file_name) const {
  auto const ts_file_name =
      std::filesystem::path{file_name.string() + ".transfer_set"};
  auto const bf_file_name =
      std::filesystem::path{file_name.string() + ".bitfields"};
  return std::make_tuple(ts_file_name, bf_file_name);
}

void tb_preprocessing::store_transfer_set(
    std::filesystem::path const& file_name) {
  auto const [ts_file_name, bf_file_name] = file_names(file_name);

  // transfer set
  auto ts_buf = cista::serialize(ts_);
  write_file(ts_file_name, ts_buf);

  // bitfields
  auto bf_buf = cista::serialize(tt_.bitfields_);
  write_file(bf_file_name, bf_buf);
}

void tb_preprocessing::load_transfer_set(
    std::filesystem::path const& file_name) {
  auto const [ts_file_name, bf_file_name] = file_names(file_name);
  std::vector<std::uint8_t> vec;

  // transfer set
  read_file(ts_file_name, vec);

  auto const ts_loaded =
      cista::deserialize<nvec<std::uint32_t, transfer, 2>>(vec);

  std::vector<std::vector<transfer>> mule;
  mule.resize(100);
  for (auto& inner_vec : mule) {
    inner_vec.reserve(64);
  }
  for (auto t = 0U; t != ts_loaded->size(); ++t) {
    auto s = 0U;
    for (; s != ts_loaded->size(t); ++s) {
      for (auto const& transfer : ts_loaded->at(t, s)) {
        mule[s].emplace_back(transfer);
      }
    }
    ts_.emplace_back(
        it_range{mule.cbegin(), mule.cbegin() + static_cast<std::int64_t>(s)});
    for (auto pos = 1U; pos != s; ++pos) {
      mule[pos].clear();
    }
  }

  // bitfields
  vec.clear();
  read_file(bf_file_name, vec);

  auto const bf_loaded =
      cista::deserialize<vector_map<bitfield_idx_t, bitfield>>(vec);

  tt_.bitfields_.clear();
  for (auto const& bf : *bf_loaded) {
    tt_.bitfields_.emplace_back(bf);
  }

  ts_ready_ = true;
}