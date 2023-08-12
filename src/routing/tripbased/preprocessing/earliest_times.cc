#include "nigiri/routing/tripbased/preprocessing/earliest_times.h"

#ifdef TB_PREPRO_TRANSFER_REDUCTION

#include "nigiri/routing/tripbased/preprocessing/dominates.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

#ifdef TB_MIN_WALK

void earliest_times::update_walk(location_idx_t location,
                                 std::uint16_t time_arr_new,
                                 std::uint16_t time_walk_new,
                                 bitfield const& bf,
                                 bitfield* impr) {
  // bitfield is manipulated during update process
  bf_new_ = bf;
  // position of entry with an equal time
  std::optional<std::uint32_t> same_spot = std::nullopt;
  // position of entry with no active days
  std::optional<std::uint32_t> overwrite_spot = std::nullopt;
  // compare to existing entries of this location
  for (auto i{0U}; i != times_[location].size(); ++i) {
    if (bf_new_.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return;
    }
    if (times_[location][i].bf_.any()) {
      auto const dom =
          dominates(time_arr_new, time_walk_new, times_[location][i].time_arr_,
                    times_[location][i].time_walk_);
      if (dom < 0) {
        // new tuple dominates
        times_[location][i].bf_ &= ~bf_new_;
      } else if (0 < dom) {
        // existing tuple dominates
        bf_new_ &= ~times_[location][i].bf_;
      } else {
        if (!(time_arr_new < times_[location][i].time_arr_ ||
              time_walk_new < times_[location][i].time_walk_)) {
          // new tuple does not improve
          bf_new_ &= ~times_[location][i].bf_;
        }
        if (time_arr_new == times_[location][i].time_arr_ &&
            time_walk_new == times_[location][i].time_walk_) {
          // remember position of same tuple
          same_spot = i;
        }
      }
    }
    if (times_[location][i].bf_.none()) {
      // remember overwrite spot
      overwrite_spot = i;
    }
  }
  // after comparison to existing entries
  if (bf_new_.any()) {
    // new entry has at least one active day after comparison
    if (same_spot.has_value()) {
      // entry with same values already exists -> add active days of new time
      // to it
      times_[location][same_spot.value()].bf_ |= bf_new_;
    } else if (overwrite_spot.has_value()) {
      // overwrite spot was found -> use for new entry
      times_[location][overwrite_spot.value()].time_arr_ = time_arr_new;
      times_[location][overwrite_spot.value()].time_walk_ = time_walk_new;
      times_[location][overwrite_spot.value()].bf_ = bf_new_;
    } else {
      // add new entry
      times_[location].emplace_back(time_arr_new, time_walk_new, bf_new_);
    }
    // add improvements to impr
    if (impr != nullptr) {
      *impr |= bf_new_;
    }
  }
}

#else

void earliest_times::update(location_idx_t location,
                            std::uint16_t time_new,
                            bitfield const& bf,
                            bitfield* impr) {
  // bitfield is manipulated during update process
  bf_new_ = bf;
  // position of entry with an equal time
  std::optional<std::uint32_t> same_time_spot = std::nullopt;
  // position of entry with no active days
  std::optional<std::uint32_t> overwrite_spot = std::nullopt;
  // compare to existing entries of this location
  for (auto i{0U}; i != times_[location].size(); ++i) {
    if (bf_new_.none()) {
      // all bits of new entry were set to zero, new entry does not improve
      // upon any times
      return;
    }
    if (time_new < times_[location][i].time_) {
      // new time is better than existing time, update bit set of existing
      // time
      times_[location][i].bf_ &= ~bf_new_;
    } else {
      // new time is greater or equal
      // remove active days from new time that are already active in the
      // existing entry
      bf_new_ &= ~times_[location][i].bf_;
      if (time_new == times_[location][i].time_) {
        // remember this position to add active days of new time after
        // comparison to existing entries
        same_time_spot = i;
      }
    }
    if (times_[location][i].bf_.none()) {
      // existing entry has no active days left -> remember as overwrite
      // spot
      overwrite_spot = i;
    }
  }
  // after comparison to existing entries
  if (bf_new_.any()) {
    // new time has at least one active day after comparison
    if (same_time_spot.has_value()) {
      // entry for this time already exists -> add active days of new time to
      // it
      times_[location][same_time_spot.value()].bf_ |= bf_new_;
    } else if (overwrite_spot.has_value()) {
      // overwrite spot was found -> use for new entry
      times_[location][overwrite_spot.value()].time_ = time_new;
      times_[location][overwrite_spot.value()].bf_ = bf_new_;
    } else {
      // add new entry
      times_[location].emplace_back(time_new, bf_new_);
    }
    // add improvements to impr
    if (impr != nullptr) {
      *impr |= bf_new_;
    }
  }
}

#endif

#endif