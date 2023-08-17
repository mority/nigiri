#include "utl/get_or_create.h"
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

void preprocessor::build(transfer_set& ts, const std::uint16_t sleep_duration) {
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
    std::this_thread::sleep_for(
        std::chrono::duration<std::uint16_t, std::milli>(sleep_duration));

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
        // start from begin, next part maybe more towards the front of the
        // queue
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
            << n_transfers_ * sizeof(transfer) << " bytes\n";

  ts.tt_hash_ = hash_tt(tt_);
  ts.num_el_con_ = num_el_con_;
  ts.route_max_length_ = route_max_length_;
  ts.transfer_time_max_ = transfer_time_max_;
  ts.n_transfers_ = n_transfers_;
  ts.ready_ = true;
}