#include "nigiri/routing/tripbased/settings.h"

#ifdef TB_PREPRO_VANILLA

#include "utl/get_or_create.h"

#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/preprocessing/earliest_times.h"
#include "nigiri/routing/tripbased/preprocessing/tb_preprocessor.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

#include <mutex>

#ifdef TB_MIN_WALK
#include "nigiri/routing/tripbased/preprocessing/dominates.h"
#endif

#ifdef TB_TRANSFER_CLASS
#include "nigiri/routing/tripbased/transfer_class.h"
#endif

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace std::chrono_literals;

void tb_preprocessor::build_part(tb_preprocessor* const pp) {

  // days of transport that still require a connection
  bitfield omega;

  // active days of current transfer
  bitfield theta;

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  earliest_times ets_arr_;
  earliest_times ets_ch_;

  // days on which the transfer constitutes an
  // improvement
  bitfield impr;
#endif

  // init a new part
  part_t part;

  while (true) {

    // get next transport index to process
    {
      std::lock_guard<std::mutex> const lock(pp->next_transport_mutex_);
      part.first = pp->next_transport_;
      if (part.first == pp->tt_.transport_traffic_days_.size()) {
        break;
      }
      ++pp->next_transport_;
    }

    transport_idx_t t{part.first};

    // route index of the current transport
    auto const route_t = pp->tt_.transport_route_[t];

    // the stops of the current transport
    auto const stop_seq_t = pp->tt_.route_location_seq_[route_t];

    // resize for number of stops of this transport
    part.second.resize(stop_seq_t.size());
    for (auto& transfers_vec : part.second) {
      transfers_vec.clear();
    }

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

      // time of day for tau_arr(t,i)
      std::uint16_t const alpha =
          pp->tt_.event_mam(t, i, event_type::kArr).mam_;

      // shift amount due to travel time of the transport we are
      // transferring from
      std::int8_t const sigma_t =
          pp->tt_.event_mam(t, i, event_type::kArr).days_;

      // the bitfield of the transport we are transferring from
      auto const& beta_t =
          pp->tt_.bitfields_[pp->tt_.transport_traffic_days_[t]];

#ifdef TB_PREPRO_TRANSFER_REDUCTION
      // tau_arr(t,i)
      std::uint16_t const tau_arr_t_i = static_cast<std::uint16_t>(
          pp->tt_.event_mam(t, i, event_type::kArr).count());

      // init the earliest times data structure
#ifdef TB_MIN_WALK
      ets_arr_.update_walk(p_t_i, tau_arr_t_i, 0, beta_t, nullptr);
      ets_ch_.update_walk(
          p_t_i, tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(),
          0, beta_t, nullptr);
      for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.update_walk(fp.target(), tau_arr_t_i + fp.duration_,
                             fp.duration_, beta_t, nullptr);
        ets_ch_.update_walk(fp.target(), tau_arr_t_i + fp.duration_,
                            fp.duration_, beta_t, nullptr);
      }
#elifdef TB_TRANSFER_CLASS
      ets_arr_.update_class(p_t_i, tau_arr_t_i, 0, beta_t, nullptr);
      ets_ch_.update_class(
          p_t_i, tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(),
          0, beta_t, nullptr);
      for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.update_class(fp.target(), tau_arr_t_i + fp.duration_, 0,
                              beta_t, nullptr);
        ets_ch_.update_class(fp.target(), tau_arr_t_i + fp.duration_, 0, beta_t,
                             nullptr);
      }
#else
      ets_arr_.update(p_t_i, tau_arr_t_i, beta_t, nullptr);
      ets_ch_.update(
          p_t_i, tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(),
          beta_t, nullptr);
      for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
        ets_arr_.update(fp.target(), tau_arr_t_i + fp.duration_, beta_t,
                        nullptr);
        ets_ch_.update(fp.target(), tau_arr_t_i + fp.duration_, beta_t,
                       nullptr);
      }
#endif
#endif

      auto handle_fp = [&sigma_t, &pp, &t, &i, &route_t, &alpha,
#ifdef TB_PREPRO_TRANSFER_REDUCTION
                        &tau_arr_t_i, &ets_arr_, &ets_ch_, &impr,
#ifdef TB_MIN_WALK
                        &p_t_i,
#endif
#endif
                        &omega, &theta, &part, &beta_t](footpath const& fp) {
        // q: location index of destination of footpath
        auto const q = fp.target();

        // arrival at stop q in alpha time scale
        std::uint16_t const tau_q = alpha + fp.duration_;
        std::uint16_t const tau_q_tod = tau_q % 1440U;
        std::int8_t const tau_q_d = static_cast<int8_t>(tau_q / 1440U);

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
              std::int8_t sigma = tau_q_d;

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
                std::uint16_t const tau_dep_alpha_u_j =
                    static_cast<std::uint16_t>(sigma) * 1440U +
                    tau_dep_u_j->mam_;

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
                  std::int8_t const sigma_u = tau_dep_u_j->days_;

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

#ifdef TB_TRANSFER_CLASS
                    auto const kappa =
                        transfer_class(tau_dep_alpha_u_j - tau_q);
                    if (kappa == 0U) {
#endif
                      // remove days that are covered by this transfer from
                      // omega
                      omega &= ~theta;
#ifdef TB_TRANSFER_CLASS
                    }
#endif

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
                            stop{pp->tt_.route_location_seq_[route_u][j + 1]}
                                .location_idx();
                        auto const p_t_prev =
                            stop{pp->tt_.route_location_seq_[route_t][i - 1]}
                                .location_idx();
                        if (p_u_next == p_t_prev) {
                          // check if u is already reachable at the
                          // previous stop of t
                          std::uint16_t const tau_dep_alpha_u_next =
                              tau_dep_alpha_u_j +
                              static_cast<std::uint16_t>(
                                  pp->tt_.event_mam(u, j + 1, event_type::kDep)
                                      .count() -
                                  pp->tt_.event_mam(u, j, event_type::kDep)
                                      .count());
                          std::uint16_t const tau_arr_alpha_t_prev =
                              alpha -
                              static_cast<std::uint16_t>(
                                  pp->tt_.event_mam(t, i, event_type::kArr)
                                      .count() -
                                  pp->tt_.event_mam(t, i - 1, event_type::kArr)
                                      .count());
                          auto const min_change_time =
                              static_cast<std::uint16_t>(
                                  pp->tt_.locations_.transfer_time_[p_t_prev]
                                      .count());
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
                      std::uint16_t const tau_dep_t_u_j =
                          tau_arr_t_i + (tau_dep_alpha_u_j - alpha);
                      for (stop_idx_t l = j + 1U;
                           l != pp->tt_.route_location_seq_[route_u].size();
                           ++l) {
                        std::uint16_t const tau_arr_t_u_l =
                            tau_dep_t_u_j +
                            static_cast<std::uint16_t>(
                                pp->tt_.event_mam(u, l, event_type::kArr)
                                    .count() -
                                tau_dep_u_j->count());
                        auto const p_u_l =
                            stop{pp->tt_.route_location_seq_[route_u][l]}
                                .location_idx();
#ifdef TB_MIN_WALK
                        std::uint16_t const walk_time_l =
                            p_t_i == p_u_j ? 0 : fp.duration_;
                        ets_arr_.update_walk(p_u_l, tau_arr_t_u_l, walk_time_l,
                                             theta, &impr);
                        ets_ch_.update_walk(
                            p_u_l,
                            tau_arr_t_u_l +
                                pp->tt_.locations_.transfer_time_[p_u_l]
                                    .count(),
                            walk_time_l, theta, &impr);
#elifdef TB_TRANSFER_CLASS
                        ets_arr_.update_class(p_u_l, tau_arr_t_u_l, kappa,
                                              theta, &impr);
                        ets_ch_.update_class(
                            p_u_l,
                            tau_arr_t_u_l +
                                pp->tt_.locations_.transfer_time_[p_u_l]
                                    .count(),
                            kappa, theta, &impr);
#else
                        ets_arr_.update(p_u_l, tau_arr_t_u_l, theta, &impr);
                        ets_ch_.update(
                            p_u_l,
                            tau_arr_t_u_l +
                                pp->tt_.locations_.transfer_time_[p_u_l]
                                    .count(),
                            theta, &impr);
#endif
                        for (auto const& fp_r :
                             pp->tt_.locations_.footpaths_out_[p_u_l]) {
                          std::uint16_t const eta =
                              tau_arr_t_u_l + fp_r.duration_;

#ifdef TB_MIN_WALK
                          std::uint16_t const walk_time_r =
                              walk_time_l + fp_r.duration_;
                          ets_arr_.update_walk(fp_r.target(), eta, walk_time_r,
                                               theta, &impr);
                          ets_ch_.update_walk(fp_r.target(), eta, walk_time_r,
                                              theta, &impr);
#elifdef TB_TRANSFER_CLASS
                          ets_arr_.update_class(fp_r.target(), eta, kappa,
                                                theta, &impr);
                          ets_ch_.update_class(fp_r.target(), eta, kappa, theta,
                                               &impr);
#else
                          ets_arr_.update(fp_r.target(), eta, theta, &impr);
                          ets_ch_.update(fp_r.target(), eta, theta, &impr);
#endif
                        }
                      }

                      std::swap(theta, impr);
                      if (theta.any()) {
#endif
#ifndef NDEBUG
                        TBDL << transfer_str(pp->tt_, t, i, u, j) << "\n";
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

#endif