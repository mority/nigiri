#include "utl/get_or_create.h"
#include "utl/parallel_for.h"
#include "utl/progress_tracker.h"

#include "nigiri/logging.h"
#include "nigiri/routing/tripbased/preprocessing/preprocessor.h"
#include "nigiri/types.h"

#include <chrono>
#include <mutex>
#include <thread>

using namespace nigiri;
using namespace nigiri::routing::tripbased;
using namespace std::chrono_literals;

bitfield_idx_t preprocessor::get_or_create_bfi(bitfield const& bf) {
  return utl::get_or_create(bitfield_to_bitfield_idx_, bf, [&bf, this]() {
    auto const bfi = tt_.register_bitfield(bf);
    bitfield_to_bitfield_idx_.emplace(bf, bfi);
    return bfi;
  });
}

void preprocessor::build(transfer_set& ts) {
  auto const num_transports = tt_.transport_traffic_days_.size();

  // progress tracker
  auto progress_tracker = utl::get_active_progress_tracker();
  progress_tracker->status("Building Transfer Set")
      .reset_bounds()
      .in_high(num_transports);

  // parallel_for
  interval<std::uint32_t> const transport_idx_interval = {0, num_transports};
  utl::parallel_for(
      transport_idx_interval,
      [&](auto const t) { build_part(transport_idx_t{t}); },
      progress_tracker->update_fn());

  // deduplicate
  progress_tracker->status("Deduplicating bitfields")
      .reset_bounds()
      .in_high(num_transports);
  std::vector<std::vector<transfer>> transfers_per_stop;
  transfers_per_stop.resize(route_max_length_);

  // iterate stop vectors of all transports
  for (auto& stop_vec : expanded_transfers_) {
    // deduplicate bitfield of each transfer
    for (unsigned s = 0U; s != stop_vec.size(); ++s) {
      for (auto const& exp_transfer : stop_vec[s]) {
        transfers_per_stop[s].emplace_back(
            get_or_create_bfi(exp_transfer.bf_).v_,
            exp_transfer.transport_idx_to_.v_, exp_transfer.stop_idx_to_,
            exp_transfer.passes_midnight_);
        ++n_transfers_;
      }
    }

    // add transfers of this transport to transfer set
    ts.data_.emplace_back(
        it_range{transfers_per_stop.cbegin(),
                 transfers_per_stop.cbegin() +
                     static_cast<std::int64_t>(stop_vec.size())});
    // clean up
    for (unsigned s = 0U; s != stop_vec.size(); ++s) {
      transfers_per_stop[s].clear();
    }

    progress_tracker->increment();
  }

  prepro_stats_.n_transfers_final_ = n_transfers_;

  prepro_stats_.print(std::cout);

  ts.tt_hash_ = hash_tt(tt_);
  ts.num_el_con_ = num_el_con_;
  ts.route_max_length_ = route_max_length_;
  ts.transfer_time_max_ = transfer_time_max_;
  ts.n_transfers_ = n_transfers_;
  ts.prepro_stats_ = prepro_stats_;
  ts.ready_ = true;
}