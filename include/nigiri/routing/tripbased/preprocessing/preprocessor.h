#pragma once

#include "boost/functional/hash.hpp"
#include "nigiri/routing/tripbased/preprocessing/expanded_transfer.h"
#include "nigiri/routing/tripbased/preprocessing/ordered_transport_id.h"
#include "nigiri/routing/tripbased/preprocessing/reached_line_based.h"
#include "nigiri/routing/tripbased/settings.h"
#include "nigiri/routing/tripbased/transfer.h"
#include "nigiri/routing/tripbased/transfer_set.h"
#include "nigiri/timetable.h"
#include <filesystem>
#include <list>
#include <vector>

namespace nigiri::routing::tripbased {

using part_t =
    std::pair<std::uint32_t, std::vector<std::vector<expanded_transfer>>>;
using queue_t = std::list<part_t>;

struct expanded_transfer;

struct preprocessor {

#ifdef TB_PREPRO_LB_PRUNING
  struct line_transfer {
    line_transfer(stop_idx_t stop_idx_from,
                  route_idx_t route_idx_to,
                  stop_idx_t stop_idx_to,
                  duration_t footpath_length)
        : stop_idx_from_(stop_idx_from),
          route_idx_to_(route_idx_to),
          stop_idx_to_(stop_idx_to),
          footpath_length_(footpath_length) {}

    stop_idx_t stop_idx_from_;
    route_idx_t route_idx_to_;
    stop_idx_t stop_idx_to_;
    duration_t footpath_length_;
  };

  struct {
    // sort neighborhood
    // 1. by target line
    // 2. by decreasing origin line index
    // 3. by increasing target line index
    bool operator()(line_transfer a, line_transfer b) const {
      return a.route_idx_to_ < b.route_idx_to_ ||
             (a.route_idx_to_ == b.route_idx_to_ &&
              a.stop_idx_from_ > b.stop_idx_from_) ||
             (a.route_idx_to_ == b.route_idx_to_ &&
              a.stop_idx_from_ == b.stop_idx_from_ &&
              a.stop_idx_to_ < b.stop_idx_to_);
    }
  } line_transfer_comp;

  void line_transfers(route_idx_t route_from,
                      std::vector<line_transfer>& neighborhood);

  void line_transfers_fp(route_idx_t route_from,
                         std::size_t i,
                         footpath fp,
                         std::vector<line_transfer>& neighborhood);
#endif

#ifdef TB_MIN_WALK
  std::uint16_t walk_time(location_idx_t a, location_idx_t b) {
    if (a != b) {
      for (auto const& fp : tt_.locations_.footpaths_out_[a]) {
        if (fp.target() == b) {
          return fp.duration_;
        }
      }
    }
    return 0;
  }
#endif

  explicit preprocessor(timetable& tt, std::uint16_t transfer_time_max = 1440)
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

#ifdef TB_PREPRO_LB_PRUNING
      prepro_stats_.line_based_pruning = true;
#endif
#ifdef TB_PREPRO_UTURN_REMOVAL
      prepro_stats_.u_turn_removal = true;
#endif
#ifdef TB_PREPRO_TRANSFER_REDUCTION
      prepro_stats_.transfer_reduction = true;
#endif

      // allocate for each transport
      expanded_transfers_.resize(tt_.transport_route_.size());
      for (size_t i = 0; i != expanded_transfers_.size(); ++i) {
        // allocate for each stop of the transport
        expanded_transfers_[i].resize(
            tt_.route_location_seq_[tt_.transport_route_[transport_idx_t{i}]]
                .size());
        for (auto& transfer_vec : expanded_transfers_[i]) {
          // reserve space for transfers at each stop
          transfer_vec.reserve(64);
        }
      }
    }
  }

  void build(transfer_set& ts, const std::uint16_t sleep_duration);

  void build_part(transport_idx_t const);

  // wrapper for utl::get_or_create
  bitfield_idx_t get_or_create_bfi(bitfield const& bf);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  // the timetable that is being processed
  timetable& tt_;

  // the number of elementary connections in the timetable
  unsigned long num_el_con_{0U};

  // length of the longest route
  std::size_t route_max_length_{0U};

  // max. look-ahead
  std::uint16_t const transfer_time_max_;

  // the number of transfers found
  std::uint64_t n_transfers_{0U};

  preprocessing_stats prepro_stats_;

  // the expanded transfer set, i.e., before bitfield deduplication
  std::vector<std::vector<std::vector<expanded_transfer>>> expanded_transfers_;
};

static inline void build_transfer_set(
    timetable& tt,
    transfer_set& ts,
    const std::uint16_t sleep_duration = 1000) {
  {
    auto const timer = scoped_timer("trip-based preprocessing");
    preprocessor tbp(tt);
    tbp.build(ts, sleep_duration);
  }
}

static inline std::size_t hash_tt(timetable const& tt) {
  std::size_t res{0U};

  boost::hash_combine(res, tt.locations_.location_id_to_idx_.size());
  boost::hash_combine(res, tt.locations_.names_.size());
  boost::hash_combine(res, tt.locations_.timezones_.size());
  boost::hash_combine(res, tt.date_range_.from_.time_since_epoch().count());
  boost::hash_combine(res, tt.date_range_.to_.time_since_epoch().count());
  boost::hash_combine(res, tt.trip_id_to_idx_.size());
  boost::hash_combine(res, tt.trip_ids_.size());
  boost::hash_combine(res, tt.source_file_names_.size());
  boost::hash_combine(res, tt.route_transport_ranges_.size());
  boost::hash_combine(res, tt.route_stop_times_.size());
  boost::hash_combine(res, tt.transport_traffic_days_.size());
  boost::hash_combine(res, tt.bitfields_.size());
  boost::hash_combine(res, tt.merged_trips_.size());
  boost::hash_combine(res, tt.attributes_.size());
  boost::hash_combine(res, tt.attribute_combinations_.size());
  boost::hash_combine(res, tt.providers_.size());
  boost::hash_combine(res, tt.trip_direction_strings_.size());
  boost::hash_combine(res, tt.trip_directions_.size());

  std::uint_fast8_t num_bf = 0U;
  for (auto bf = tt.bitfields_.rbegin(); bf != tt.bitfields_.rend(); ++bf) {
    for (auto const& block : bf->blocks_) {
      boost::hash_combine(res, block);
    }
    if (++num_bf == 10) {
      break;
    }
  }

  return res;
}

}  // namespace nigiri::routing::tripbased