#pragma once

#include "nigiri/timetable.h"
#include <filesystem>
#include "bits.h"
#include "transfer.h"
#include "transfer_set.h"

#define TB_PREPRO_UTURN_REMOVAL
#define TB_PREPRO_TRANSFER_REDUCTION

namespace nigiri::routing::tripbased {

struct tb_preprocessor {

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  struct earliest_times {
    struct earliest_time {
      duration_t time_{};
      bitfield bf_{};
    };

    explicit earliest_times(tb_preprocessor& tbp) : tbp_(tbp) {}

    bool update(location_idx_t li_new, duration_t time_new, bitfield const& bf);

    auto size() const noexcept { return location_idx_times_.size(); }

    void clear() noexcept { location_idx_times_.clear(); }

    tb_preprocessor& tbp_;
    // temp for get_create_bfi
    bitfield bf_new_;
    mutable_fws_multimap<location_idx_t, earliest_time> location_idx_times_{};
  };
#endif

  //  preprocessor() = delete;
  explicit tb_preprocessor(timetable& tt,
                           duration_t transfer_time_max = duration_t{1440U})
      : tt_(tt), transfer_time_max_(transfer_time_max) {
    {
      auto const timer = scoped_timer("trip-based preprocessing: init");

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
          std::cerr << "WARNING: number of stops exceeds maximum value of stop "
                       "index\n";
        }
      }
      static_assert(kMaxDays <= kDayIdxMax);

      // count elementary connections and longest route
      for (route_idx_t route_idx{0U};
           route_idx < tt_.route_location_seq_.size(); ++route_idx) {
        num_el_con_ += (tt_.route_location_seq_[route_idx].size() - 1) *
                       tt_.route_transport_ranges_[route_idx].size().v_;
        if (route_max_length_ < tt_.route_location_seq_[route_idx].size()) {
          route_max_length_ = tt_.route_location_seq_[route_idx].size();
        }
      }

      // init bitfields hashmap with bitfields that are already used by the
      // timetable
      for (bitfield_idx_t bfi{0U}; bfi < tt_.bitfields_.size();
           ++bfi) {  // bfi: bitfield index
        bitfield_to_bitfield_idx_.emplace(tt_.bitfields_[bfi], bfi);
      }

      // number of expected transfers
      //    auto const num_exp_transfers = num_el_con_;
      // reserve space for transfer set
      //    std::cout << "Reserving " << num_exp_transfers * sizeof(transfer)
      //              << " bytes for " << num_exp_transfers << " expected
      //              transfers\n";
    }
  }

  void build(transfer_set& ts);

  // wrapper for utl::get_or_create
  bitfield_idx_t get_or_create_bfi(bitfield const& bf);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  // the timetable that is being processed
  timetable& tt_;

  // the number of elementary connections in the timetable
  unsigned num_el_con_ = 0U;

  // length of the longest route
  std::size_t route_max_length_ = 0U;

  // max. look-ahead
  duration_t const transfer_time_max_;

  // the number of transfers found
  unsigned n_transfers_ = 0U;
};

static void build_transfer_set(timetable& tt, transfer_set& ts) {
  {
    auto const timer = scoped_timer("trip-based preprocessing");
    tb_preprocessor tbp(tt);
    tbp.build(ts);
  }
}

}  // namespace nigiri::routing::tripbased