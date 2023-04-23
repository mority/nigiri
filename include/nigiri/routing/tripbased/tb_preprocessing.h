#pragma once

#include "nigiri/timetable.h"
#include "tb.h"

namespace nigiri::routing::tripbased {

struct tb_preprocessing {

  struct earliest_time {
    constexpr earliest_time(const location_idx_t& location,
                            const duration_t& time,
                            const bitfield_idx_t& bf_idx)
        : location_idx_(location), time_(time), bf_idx_(bf_idx) {}

    location_idx_t location_idx_{};
    duration_t time_{};
    bitfield_idx_t bf_idx_{};
  };

  struct earliest_times {
    earliest_times() = delete;
    explicit earliest_times(tb_preprocessing& tbp) : tbp_(tbp) {}

    bool update(location_idx_t li_new,
                duration_t time_new,
                bitfield const& bf) {

      bitfield bf_new = bf;

      // find first tuple of this li_new
      auto et_cur = std::lower_bound(
          data_.begin(), data_.end(), li_new,
          [](earliest_time const& et, location_idx_t const& l) constexpr {
            return et.location_idx_ < l;
          });

      // no entry for this li_new
      if (et_cur == data_.end()) {
        data_.emplace_back(li_new, time_new, tbp_.get_or_create_bfi(bf_new));
        return true;
      } else if (li_new < et_cur->location_idx_) {
        data_.emplace(et_cur, li_new, time_new, tbp_.get_or_create_bfi(bf_new));
        return true;
      }

      auto overwrite_spot = data_.end();

      // iterate entries for this li_new
      while (et_cur != data_.end() && et_cur->location_idx_ == li_new) {
        if (bf_new.none()) {
          // all bits of new entry were set to zero, new entry does not improve
          // upon any times
          return false;
        } else if (time_new < et_cur->time_) {
          // new time is better than current time, update bit set of current
          // time
          bitfield bf_cur = tbp_.tt_.bitfields_[et_cur->bf_idx_] & ~bf_new;
          if (bf_cur.none()) {
            // if current time is no longer needed, we remove it from the vector
            if (overwrite_spot == data_.end()) {
              overwrite_spot = et_cur;
              ++et_cur;
            } else {
              data_.erase(et_cur);
            }
          } else {
            // save updated bitset index of current time
            et_cur->bf_idx_ = tbp_.get_or_create_bfi(bf_cur);
            ++et_cur;
          }
        } else if (time_new == et_cur->time_) {
          // entry for this time already exists
          if ((bf_new & ~tbp_.tt_.bitfields_[et_cur->bf_idx_]).none()) {
            // all bits of bf_new already covered
            return false;
          } else {
            bf_new |= tbp_.tt_.bitfields_[et_cur->bf_idx_];
            if (overwrite_spot == data_.end()) {
              overwrite_spot = et_cur;
              ++et_cur;
            } else {
              data_.erase(et_cur);
            }
          }
        } else {
          // new time is worse than current time, update bit set of new time
          bf_new &= ~tbp_.tt_.bitfields_[et_cur->bf_idx_];
          ++et_cur;
        }
      }

      if (bf_new.any()) {
        if (overwrite_spot == data_.end()) {
          data_.emplace(et_cur, li_new, time_new,
                        tbp_.get_or_create_bfi(bf_new));
        } else {
          overwrite_spot->time_ = time_new;
          overwrite_spot->bf_idx_ = tbp_.get_or_create_bfi(bf_new);
        }
        return true;
      } else {
        return false;
      }
    }

    constexpr unsigned long size() { return data_.size(); }

    constexpr earliest_time& operator[](unsigned long pos) {
      return data_[pos];
    }

    tb_preprocessing& tbp_;
    std::vector<earliest_time> data_{};
  };

  tb_preprocessing() = delete;
  explicit tb_preprocessing(timetable& tt, int sa_w_max = 1)
      : tt_(tt), sa_w_max_(sa_w_max) {}

  // preprocessing without reduction step
  void build_transfer_set(
      bool uturn_removal = true /*, bool reduction = true*/);

  // load precomputed transfer set from file
  // also needs to load the corresponding timetable from file since bitfields
  // of the transfers are stored in the timetable
  void load_transfer_set(/* file name */);

  // map a bitfield to its bitfield_idx
  // init with bitfields of timetable
  hash_map<bitfield, bitfield_idx_t> bitfield_to_bitfield_idx_{};

  bitfield_idx_t get_or_create_bfi(bitfield const& bf);

  timetable& tt_;
  int const sa_w_max_{};  // look-ahead
  hash_transfer_set ts_{};
};

}  // namespace nigiri::routing::tripbased