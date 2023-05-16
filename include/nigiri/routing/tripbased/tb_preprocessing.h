#pragma once

#include "nigiri/timetable.h"
#include "tb.h"

namespace nigiri::routing::tripbased {

struct tb_preprocessing {

  struct earliest_time {
    constexpr earliest_time(location_idx_t const location,
                            duration_t const time,
                            bitfield_idx_t const bf_idx)
        : location_idx_(location), time_(time), bf_idx_(bf_idx) {}

    location_idx_t location_idx_{};
    duration_t time_{};
    bitfield_idx_t bf_idx_{};
  };

  struct earliest_times {
    earliest_times() = delete;
    explicit earliest_times(tb_preprocessing& tbp) : tbp_(tbp) {}

    bool update(location_idx_t li_new, duration_t time_new, bitfield const& bf);

    constexpr unsigned long size() const noexcept { return data_.size(); }

    constexpr earliest_time& operator[](unsigned long pos) {
      return data_[pos];
    }

    constexpr void clear() noexcept { data_.clear(); }

    tb_preprocessing& tbp_;
    std::vector<earliest_time> data_{};
  };

  tb_preprocessing() = delete;
  explicit tb_preprocessing(timetable& tt, day_idx_t sa_w_max = day_idx_t{1U})
      : tt_(tt), sa_w_max_(sa_w_max) {

    // check system limits
    assert(tt.bitfields_.size() <= kBitfieldIdxMax);
    if (tt.bitfields_.size() > kBitfieldIdxMax) {
      std::cerr << "WARNING: number of bitfields exceeds maximum value of "
                   "bitfield index\n";
    }
    assert(tt.transport_route_.size() <= kTransportIdxMax);
    if (tt.transport_route_.size() > kTransportIdxMax) {
      std::cerr << "WARNING: number of transports exceeds maximum value of "
                   "transport index\n";
    }
    for (auto const stop_seq : tt_.route_location_seq_) {
      assert(stop_seq.size() <= kStopIdxMax);
      if (stop_seq.size() > kStopIdxMax) {
        std::cerr
            << "WARNING: number of stops exceeds maximum value of stop index\n";
      }
    }
    static_assert(kMaxDays <= kDayIdxMax);

    // count elementary connections
    for (route_idx_t route_idx{0U}; route_idx < tt_.route_location_seq_.size();
         ++route_idx) {
      num_el_con_ += (tt_.route_location_seq_[route_idx].size() - 1) *
                     tt_.route_transport_ranges_[route_idx].size().v_;
    }

    // number of expected transfers
    auto const num_exp_transfers = num_el_con_;

    // reserve space for transfer set
    std::cout << "Reserving " << num_exp_transfers * sizeof(transfer)
              << " bytes for " << num_exp_transfers << " expected transfers\n";
    ts_.transfers_.reserve(num_exp_transfers);
  }

  void build_transfer_set(bool uturn_removal = true, bool reduction = true);

  // load precomputed transfer set from file
  // also needs to load the corresponding timetable from file since bitfields
  // of the transfers are stored in the timetable
  void load_transfer_set(/* file name */);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  bitfield_idx_t get_or_create_bfi(bitfield const& bf);

  timetable& tt_;
  // the number of elementary connections in the timetable
  unsigned num_el_con_;
  day_idx_t const sa_w_max_{};  // look-ahead
  hash_transfer_set ts_{};
};

}  // namespace nigiri::routing::tripbased