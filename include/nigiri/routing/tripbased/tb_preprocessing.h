#pragma once

#include "nigiri/timetable.h"
#include <filesystem>
#include <fstream>
#include "tb.h"

#define TB_PREPRO_UTURN_REMOVAL
#define TB_PREPRO_TRANSFER_REDUCTION

namespace nigiri::routing::tripbased {

struct tb_preprocessing {

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  struct earliest_times {
    struct earliest_time {
      duration_t time_{};
      bitfield_idx_t bf_idx_{};
    };

    earliest_times() = delete;
    explicit earliest_times(tb_preprocessing& tbp) : tbp_(tbp) {}

    bool update(location_idx_t li_new, duration_t time_new, bitfield const& bf);

    auto size() const noexcept { return location_idx_times_.size(); }

    void clear() noexcept { location_idx_times_.clear(); }

    tb_preprocessing& tbp_;
    mutable_fws_multimap<location_idx_t, earliest_time> location_idx_times_{};
  };
#endif

  tb_preprocessing() = delete;
  explicit tb_preprocessing(timetable& tt, day_idx_t sa_w_max = day_idx_t{1U})
      : tt_(tt),
        sigma_w_max_(sa_w_max)
#ifdef TB_PREPRO_TRANSFER_REDUCTION
        ,
        ets_arr_(*this),
        ets_ch_(*this)
#endif
  {

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

    // count elementary connections and longest route
    for (route_idx_t route_idx{0U}; route_idx < tt_.route_location_seq_.size();
         ++route_idx) {
      num_el_con_ += (tt_.route_location_seq_[route_idx].size() - 1) *
                     tt_.route_transport_ranges_[route_idx].size().v_;
      if (route_max_length < tt_.route_location_seq_[route_idx].size()) {
        route_max_length = tt_.route_location_seq_[route_idx].size();
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

  void build_transfer_set();

  auto file_names(std::filesystem::path const& file_name) const;

  // stores the transfers set and the bitfields in a file
  void store_transfer_set(std::filesystem::path const& file_name);

  // load precomputed transfer set from file
  // also needs to load the corresponding timetable from file since
  // bitfields of the transfers are stored in the timetable
  void load_transfer_set(std::filesystem::path const& file_name);

  // writes the content of the vector to the specified file
  static void write_file(std::filesystem::path const& file_name,
                         std::vector<std::uint8_t> const& vec) {
    std::ofstream ofs(file_name,
                      std::ios::out | std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
      ofs.write(reinterpret_cast<const char*>(vec.data()),
                vec.empty()
                    ? 0
                    : static_cast<std::int64_t>(sizeof(vec[0]) * vec.size()));
      ofs.close();
    } else {
      std::cout << "Could not ope file: " << file_name << std::endl;
    }
  }

  // reads the specified file and stores its content in the vector given
  static void read_file(std::filesystem::path const& file_name,
                        std::vector<std::uint8_t>& vec) {
    std::ifstream ifs(file_name,
                      std::ios::in | std::ios::binary | std::ios::ate);
    if (ifs.is_open()) {
      auto length = ifs.tellg();
      ifs.seekg(0, std::ios::beg);
      vec.resize(static_cast<std::uint64_t>(length));
      ifs.read(reinterpret_cast<char*>(vec.data()), length);
      ifs.close();
    } else {
      std::cout << "Could not ope file: " << file_name << std::endl;
    }
  }

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
  std::size_t route_max_length = 0U;
  // max. look-ahead
  day_idx_t const sigma_w_max_{};
  // the number of transfers found
  unsigned n_transfers_ = 0U;
  // the transfer set
  nvec<std::uint32_t, transfer, 2> ts_;

#ifdef TB_PREPRO_TRANSFER_REDUCTION
  earliest_times ets_arr_;
  earliest_times ets_ch_;
#endif
};

}  // namespace nigiri::routing::tripbased