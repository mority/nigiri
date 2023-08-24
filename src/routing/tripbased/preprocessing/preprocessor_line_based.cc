#include "nigiri/routing/tripbased/settings.h"

#ifdef TB_PREPRO_LB_PRUNING

#include "utl/get_or_create.h"
#include "utl/progress_tracker.h"

#include "nigiri/logging.h"
#include "nigiri/routing/tripbased/dbg.h"
#ifdef TB_TRANSFER_CLASS
#include "nigiri/routing/tripbased/transfer_class.h"
#endif
#include "nigiri/routing/tripbased/preprocessing/preprocessor.h"
#include "nigiri/routing/tripbased/preprocessing/reached_reduction.h"
#include "nigiri/stop.h"
#include "nigiri/types.h"

#include <chrono>
#include <mutex>
#include <thread>

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace std::chrono_literals;

void preprocessor::build_part(preprocessor* const pp) {

  // days of transport that still require a connection
  bitfield omega;

  // active days of current transfer
  bitfield theta;
  bitfield theta_prime;

  // the index of the route that we are currently processing
  route_idx_t current_route;

  // init neighborhood
  std::vector<preprocessor::line_transfer> neighborhood;
  neighborhood.reserve(pp->route_max_length_ * 10);

  // earliest transport per stop index
  reached_line_based et;

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  reached_reduction rr_arr;
  reached_reduction rr_ch;

  // days on which the transfer constitutes an
  // improvement
  bitfield impr;
#endif

  while (true) {
    // get next route index to process
    {
      std::lock_guard<std::mutex> const lock(pp->next_route_mutex_);
      current_route = route_idx_t{pp->next_route_};
      if (current_route == pp->tt_.n_routes()) {
        break;
      }
      ++pp->next_route_;
    }

    // build neighborhood of current route
    neighborhood.clear();
    pp->line_transfers(current_route, neighborhood);

    // the stops of the current route
    auto const stop_seq_from = pp->tt_.route_location_seq_[current_route];

    // get transports of current route
    auto const& route_transports =
        pp->tt_.route_transport_ranges_[current_route];

    // iterate transports of the current route
    for (auto const t : route_transports) {
#ifndef NDEBUG
      TBDL << "Processing transport " << t << "\n";
#endif

      // partial transfer set for this transport
      part_t part;
      part.first = t.v_;
      // resize for number of stops of this transport
      part.second.resize(stop_seq_from.size());
      for (auto& transfers_vec : part.second) {
        transfers_vec.clear();
      }

      if (!neighborhood.empty()) {
        // the bitfield of the transport we are transferring from
        auto const& beta_t =
            pp->tt_.bitfields_[pp->tt_.transport_traffic_days_[t]];

        // the previous target route
        route_idx_t route_to_prev = neighborhood[0].route_idx_to_;
        // stop sequence of the route we are transferring to
        auto stop_seq_to = pp->tt_.route_location_seq_[route_to_prev];
        // initial reset of earliest transport
#ifdef TB_MIN_WALK
        et.reset_walk(stop_seq_to.size());
#elifdef TB_TRANSFER_CLASS
        et.reset_class(stop_seq_to.size());
#else
        et.reset(stop_seq_to.size());
#endif
        // iterate entries in route neighborhood
        for (auto const& neighbor : neighborhood) {

          // handle change of target line
          if (route_to_prev != neighbor.route_idx_to_) {
            route_to_prev = neighbor.route_idx_to_;
            stop_seq_to = pp->tt_.route_location_seq_[route_to_prev];
#ifdef TB_MIN_WALK
            et.reset_walk(stop_seq_to.size());
#elifdef TB_TRANSFER_CLASS
            et.reset_class(stop_seq_to.size());
#else
            et.reset(stop_seq_to.size());
#endif
          }

#ifndef NDEBUG
          TBDL << "Examining neighbor (" << neighbor.stop_idx_from_ << ", "
               << neighbor.route_idx_to_ << ", " << neighbor.stop_idx_to_
               << ", " << neighbor.footpath_length_ << ")\n";
#endif

          auto const tau_arr_t_i =
              pp->tt_.event_mam(t, neighbor.stop_idx_from_, event_type::kArr);
          auto const alpha = tau_arr_t_i.mam();
          auto const sigma_t = static_cast<std::int8_t>(tau_arr_t_i.days());
          auto const tau_q = static_cast<std::uint16_t>(
              alpha + neighbor.footpath_length_.count());
          auto const tau_q_tod = tau_q % 1440;
          auto sigma_fpw = static_cast<std::int8_t>(tau_q / 1440);

#ifndef NDEBUG
          TBDL << "Transport " << t
               << " arrives at source stop: " << dhhmm(alpha)
               << ", earliest possible departure at target stop: "
               << dhhmm(tau_q) << "\n";
#endif

          // departure times of transports of target route at stop j
          auto const event_times = pp->tt_.event_times_at_stop(
              neighbor.route_idx_to_, neighbor.stop_idx_to_, event_type::kDep);

          // find first departure at or after a
          // departure time of current transport_to
          auto tau_dep_u_j = std::lower_bound(
              event_times.begin(), event_times.end(), tau_q_tod,
              [&](auto&& x, auto&& y) { return x.mam_ < y; });

          // no departure on this day at or after a
          if (tau_dep_u_j == event_times.end()) {
            ++sigma_fpw;  // start looking on the following day
            tau_dep_u_j = event_times.begin();  // with the earliest transport
          }

          // days that still require earliest connecting transport
          omega = beta_t;
          while (omega.any()) {

            // departure time of current transport in relation to time
            // alpha
            auto const tau_dep_alpha_u_j = static_cast<std::uint16_t>(
                sigma_fpw * 1440 + tau_dep_u_j->mam_);

            // check if max transfer time is exceeded
            if (tau_dep_alpha_u_j - alpha > pp->transfer_time_max_) {
              break;
            }

            // offset from begin of tp_to interval
            auto const k = std::distance(event_times.begin(), tau_dep_u_j);

            // transport index of transport that we transfer to
            auto const u =
                pp->tt_.route_transport_ranges_[neighbor.route_idx_to_]
                                               [static_cast<std::size_t>(k)];

#ifndef NDEBUG
            TBDL << "Transport " << u
                 << " departs at target stop: " << dhhmm(tau_dep_alpha_u_j)
                 << "\n";
#endif

            // shift amount due to number of times transport u passed
            // midnight
            auto const sigma_u = static_cast<std::int8_t>(tau_dep_u_j->days());

            // total shift amount
            std::int8_t const sigma_total = sigma_u - sigma_t - sigma_fpw;

            // bitfield transport to
            auto const& beta_u =
                pp->tt_.bitfields_[pp->tt_.transport_traffic_days_[u]];

            // init theta
            theta = omega;
            // align bitfields and perform AND
            if (sigma_total < 0) {
              theta &= beta_u >> static_cast<unsigned>(-1 * sigma_total);
            } else {
              theta &= beta_u << static_cast<unsigned>(sigma_total);
            }

            // check for match
            if (theta.any()) {
#ifdef TB_TRANSFER_CLASS
              auto const kappa = transfer_class(tau_dep_alpha_u_j - tau_q);
              if (kappa == 0) {
#endif
                // remove days that are covered by this transport from omega
                omega &= ~theta;
#ifdef TB_TRANSFER_CLASS
              }
#endif
              // update earliest transport data structure

#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
              auto const otid =
                  compute_otid(sigma_t + sigma_fpw - sigma_u,
                               pp->tt_.event_mam(u, 0, event_type::kDep).mam_);
#endif

#ifdef TB_MIN_WALK
              auto const p_t_i =
                  stop{stop_seq_from[neighbor.stop_idx_from_]}.location_idx();
              auto const p_u_j =
                  stop{stop_seq_to[neighbor.stop_idx_to_]}.location_idx();
              // compute walking time between the stops
              auto const walk_time = pp->walk_time(p_t_i, p_u_j);

              et.update_walk(neighbor.stop_idx_to_, otid, walk_time, theta);
#elifdef TB_TRANSFER_CLASS
              et.update_class(neighbor.stop_idx_to_, otid, kappa, theta);
#else
              et.update(neighbor.stop_idx_to_, sigma_t + sigma_fpw - sigma_u,
                        pp->tt_.event_mam(u, 0, event_type::kDep).mam_, theta);
#endif
              // recheck theta
              if (theta.any()) {
#ifndef NDEBUG
                TBDL << "Adding transfer: (transport " << t << ", stop "
                     << neighbor.stop_idx_from_ << ") -> (transport " << u
                     << ", stop " << neighbor.stop_idx_to_ << ")\n";
#endif

                // add transfer to set
                part.second[neighbor.stop_idx_from_].emplace_back(
                    theta, u, neighbor.stop_idx_to_, sigma_fpw);

                // add earliest transport entry
#ifdef TB_MIN_WALK
                et.transports_[neighbor.stop_idx_to_].emplace_back(
                    otid, walk_time, theta);
#elifdef TB_TRANSFER_CLASS
                et.transports_[neighbor.stop_idx_to_].emplace_back(otid, kappa,
                                                                   theta);
#else
                et.transports_[neighbor.stop_idx_to_].emplace_back(
                    sigma_t + sigma_fpw - sigma_u,
                    pp->tt_.event_mam(u, 0, event_type::kDep).mam_, theta);
#endif
                // update subsequent stops
                for (stop_idx_t j_prime = neighbor.stop_idx_to_ + 1U;
                     j_prime < stop_seq_to.size(); ++j_prime) {
                  theta_prime = theta;
#ifdef TB_MIN_WALK
                  et.update_walk(j_prime, otid, walk_time, theta_prime);
#elifdef TB_TRANSFER_CLASS
                  et.update_class(j_prime, otid, kappa, theta_prime);
#else
                  et.update(j_prime, sigma_t + sigma_fpw - sigma_u,
                            pp->tt_.event_mam(u, 0, event_type::kDep).mam_,
                            theta_prime);
#endif
                  if (theta_prime.any()) {
#ifdef TB_MIN_WALK
                    et.transports_[j_prime].emplace_back(otid, walk_time,
                                                         theta_prime);
#elifdef TB_TRANSFER_CLASS
                    et.transports_[j_prime].emplace_back(otid, kappa,
                                                         theta_prime);
#else
                    et.transports_[j_prime].emplace_back(
                        sigma_t + sigma_fpw - sigma_u,
                        pp->tt_.event_mam(u, 0, event_type::kDep).mam_,
                        theta_prime);
#endif
                  }
                }
              }
            }

            // prep next iteration
            // is this the last transport of the day?
            if (std::next(tau_dep_u_j) == event_times.end()) {

              // passing midnight
              ++sigma_fpw;

              // start with the earliest transport on the next day
              tau_dep_u_j = event_times.begin();
            } else {
              ++tau_dep_u_j;
            }
          }
        }

#ifdef TB_PREPRO_TRANSFER_REDUCTION
        // clear reached reduction
        rr_arr.reset();
        rr_ch.reset();

        // reverse iteration
        for (auto i = static_cast<stop_idx_t>(stop_seq_from.size() - 1U);
             i != 0U; --i) {
          // skip stop if exiting is not allowed
          if (!stop{stop_seq_from[i]}.out_allowed()) {
            continue;
          }

          // the location index from which we are transferring
          auto const p_t_i = stop{stop_seq_from[i]}.location_idx();

          // tau_arr(t,i)
          std::uint16_t const tau_arr_t_i = static_cast<std::uint16_t>(
              pp->tt_.event_mam(t, i, event_type::kArr).count());

          // time of day for tau_arr(t,i)
          std::uint16_t const alpha =
              pp->tt_.event_mam(t, i, event_type::kArr).mam_;

          // init the reached reduction data structure
#ifdef TB_MIN_WALK
          rr_arr.update_walk(p_t_i, tau_arr_t_i, 0, beta_t, nullptr);
          rr_ch.update_walk(
              p_t_i,
              tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(), 0,
              beta_t, nullptr);
          for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
            rr_arr.update_walk(fp.target(), tau_arr_t_i + fp.duration_,
                               fp.duration_, beta_t, nullptr);
            rr_ch.update_walk(fp.target(), tau_arr_t_i + fp.duration_,
                              fp.duration_, beta_t, nullptr);
          }
#elifdef TB_TRANSFER_CLASS
          rr_arr.update_class(p_t_i, tau_arr_t_i, 0, beta_t, nullptr);
          rr_ch.update_class(
              p_t_i,
              tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(), 0,
              beta_t, nullptr);
          for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
            rr_arr.update_class(fp.target(), tau_arr_t_i + fp.duration_, 0,
                                beta_t, nullptr);
            rr_ch.update_class(fp.target(), tau_arr_t_i + fp.duration_, 0,
                               beta_t, nullptr);
          }
#else
          rr_arr.update(p_t_i, tau_arr_t_i, beta_t, nullptr);
          rr_ch.update(
              p_t_i,
              tau_arr_t_i + pp->tt_.locations_.transfer_time_[p_t_i].count(),
              beta_t, nullptr);
          for (auto const& fp : pp->tt_.locations_.footpaths_out_[p_t_i]) {
            rr_arr.update(fp.target(), tau_arr_t_i + fp.duration_, beta_t,
                          nullptr);
            rr_ch.update(fp.target(), tau_arr_t_i + fp.duration_, beta_t,
                         nullptr);
          }
#endif
          // iterate transfers found by line-based pruning
          for (auto transfer = part.second[i].begin();
               transfer != part.second[i].end();) {
            impr.reset();

            // get route of transport that we transfer to
            auto const route_u =
                pp->tt_.transport_route_[transfer->transport_idx_to_];

            // convert departure into timescale of transport t
            auto const tau_dep_u_j =
                pp->tt_.event_mam(transfer->transport_idx_to_,
                                  transfer->stop_idx_to_, event_type::kDep);
            std::uint16_t const tau_dep_alpha_u_j =
                transfer->passes_midnight_ * 1440U + tau_dep_u_j.mam_;
            std::uint16_t const tau_dep_t_u_j =
                tau_arr_t_i + (tau_dep_alpha_u_j - alpha);
            for (stop_idx_t l = transfer->stop_idx_to_ + 1U;
                 l != pp->tt_.route_location_seq_[route_u].size(); ++l) {
              std::uint16_t const tau_arr_t_u_l =
                  tau_dep_t_u_j +
                  static_cast<std::uint16_t>(
                      pp->tt_
                          .event_mam(transfer->transport_idx_to_, l,
                                     event_type::kArr)
                          .count() -
                      tau_dep_u_j.count());

              // locations after p_u_j
              auto const p_u_l =
                  stop{pp->tt_.route_location_seq_[route_u][l]}.location_idx();

#if defined(TB_MIN_WALK) || defined(TB_TRANSFER_CLASS)
              // target location of the transfer
              auto const p_u_j =
                  stop{pp->tt_.route_location_seq_[route_u]
                                                  [transfer->stop_idx_to_]}
                      .location_idx();
#endif

#ifdef TB_MIN_WALK
              std::uint16_t walk_time_l{0U};
              if (p_t_i != p_u_j) {
                // changed stations during transfer -> find footpath of transfer
                for (auto const& fp :
                     pp->tt_.locations_.footpaths_out_[p_t_i]) {
                  if (fp.target() == p_u_j) {
                    walk_time_l = fp.duration_;
                    break;
                  }
                }
              }
              rr_arr.update_walk(p_u_l, tau_arr_t_u_l, walk_time_l,
                                 transfer->bf_, &impr);
              rr_ch.update_walk(
                  p_u_l,
                  tau_arr_t_u_l +
                      pp->tt_.locations_.transfer_time_[p_u_l].count(),
                  walk_time_l, transfer->bf_, &impr);
#elifdef TB_TRANSFER_CLASS
              std::uint16_t walk_or_change = 0;
              if (p_t_i == p_u_j) {
                walk_or_change =
                    pp->tt_.locations_.transfer_time_[p_t_i].count();
              } else {
                for (auto const& fp :
                     pp->tt_.locations_.footpaths_out_[p_t_i]) {
                  if (fp.target() == p_u_j) {
                    walk_or_change = fp.duration_;
                  }
                }
              }

              auto const kappa =
                  transfer_class(tau_dep_alpha_u_j - (alpha + walk_or_change));

              rr_arr.update_class(p_u_l, tau_arr_t_u_l, kappa, transfer->bf_,
                                  &impr);
              rr_ch.update_class(
                  p_u_l,
                  tau_arr_t_u_l +
                      pp->tt_.locations_.transfer_time_[p_u_l].count(),
                  kappa, transfer->bf_, &impr);
#else
              rr_arr.update(p_u_l, tau_arr_t_u_l, transfer->bf_, &impr);
              rr_ch.update(p_u_l,
                           tau_arr_t_u_l +
                               pp->tt_.locations_.transfer_time_[p_u_l].count(),
                           transfer->bf_, &impr);
#endif
              for (auto const& fp_r :
                   pp->tt_.locations_.footpaths_out_[p_u_l]) {
                std::uint16_t const eta = tau_arr_t_u_l + fp_r.duration_;
#ifdef TB_MIN_WALK
                std::uint16_t const walk_time_r = walk_time_l + fp_r.duration_;
                rr_arr.update_walk(fp_r.target(), eta, walk_time_r,
                                   transfer->bf_, &impr);
                rr_ch.update_walk(fp_r.target(), eta, walk_time_r,
                                  transfer->bf_, &impr);
#elifdef TB_TRANSFER_CLASS
                rr_arr.update_class(fp_r.target(), eta, kappa, transfer->bf_,
                                    &impr);
                rr_ch.update_class(fp_r.target(), eta, kappa, transfer->bf_,
                                   &impr);
#else
                rr_arr.update(fp_r.target(), eta, transfer->bf_, &impr);
                rr_ch.update(fp_r.target(), eta, transfer->bf_, &impr);
#endif
              }
            }

            std::swap(transfer->bf_, impr);

            // if the transfer offers no improvement
            if (transfer->bf_.none()) {
              // remove it
              transfer = part.second[i].erase(transfer);
            } else {
              ++transfer;
            }
          }
        }
#endif
      }
      // add part to queue
      {
        std::lock_guard<std::mutex> const lock(pp->parts_mutex_);
        pp->parts_.push_back(std::move(part));
      }
    }
  }
}

void preprocessor::line_transfers(
    route_idx_t route_from,
    std::vector<preprocessor::line_transfer>& neighborhood) {
  // stop sequence of the source route
  auto const& stop_seq = tt_.route_location_seq_[route_from];

  // examine stops of the line in reverse order, skip first stop
  for (auto i = stop_seq.size() - 1U; i >= 1; --i) {

    // skip stop if exiting is not allowed
    if (!stop{stop_seq[i]}.out_allowed()) {
      continue;
    }

    // location from which we transfer
    auto const location_from = stop{stop_seq[i]}.location_idx();

    // reflexive footpath
    line_transfers_fp(
        route_from, i,
        footpath{location_from, tt_.locations_.transfer_time_[location_from]},
        neighborhood);

    // outgoing footpaths
    for (auto const& fp : tt_.locations_.footpaths_out_[location_from]) {
      line_transfers_fp(route_from, i, fp, neighborhood);
    }
  }

  // sort neighborhood
  std::sort(neighborhood.begin(), neighborhood.end(), line_transfer_comp);

#ifndef NDEBUG
  TBDL << "build neighborhood of route " << route_from
       << " with size = " << neighborhood.size() << "\n";
#endif
}

void preprocessor::line_transfers_fp(
    route_idx_t route_from,
    std::size_t i,
    footpath fp,
    std::vector<preprocessor::line_transfer>& neighborhood) {
  // stop sequence of the source route
  auto const& stop_seq_from = tt_.route_location_seq_[route_from];
  // routes that serve the target of the footpath
  auto const& routes_at_target = tt_.location_routes_[fp.target()];
  // handle all routes that serve the target of the footpath
  for (auto const& route_to : routes_at_target) {
    // stop sequence of the target route
    auto const& stop_seq_to = tt_.route_location_seq_[route_to];
    // find the stop indices at which the target route serves the stop
    for (std::size_t j = 0; j < stop_seq_to.size() - 1; ++j) {
      // target location of the transfer
      auto const location_idx_to = stop{stop_seq_to[j]}.location_idx();
      if (location_idx_to == fp.target() && stop{stop_seq_to[j]}.in_allowed()) {
#ifndef NDEBUG
        TBDL << "Checking for U-turn transfer: (" << i << ", " << route_to
             << ", " << j << ", " << fp.duration() << ")\n";
#endif

        // check for U-turn transfer
        bool is_uturn = false;
        if (j + 1 < stop_seq_to.size() - 1) {
          auto const location_from_prev =
              stop{stop_seq_from[i - 1]}.location_idx();
          auto const location_to_next = stop{stop_seq_to[j + 1]}.location_idx();
          // next location of route_to is previous location of route_from?
          if (location_from_prev == location_to_next) {
#ifndef NDEBUG
            TBDL << "Next location of route_to is previous location of "
                    "route_from\n";
#endif
            // check if change time of alternative transfer is equal or
            // less
            is_uturn = tt_.locations_.transfer_time_[location_from_prev] <=
                       fp.duration();
          }
        }
        if (!is_uturn) {
          // add to neighborhood
          neighborhood.emplace_back(i, route_to, j, fp.duration());
        }
#ifndef NDEBUG
        else {
          TBDL << "Discarded U-turn transfer: (" << i << ", " << route_to
               << ", " << j << ", " << fp.duration() << ")\n";
        }
#endif
      }
    }
  }
}

#endif