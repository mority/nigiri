#include "../../../include/nigiri/routing/tripbased/tb.h"

namespace nigiri::routing::tripbased {

void hash_transfer_set::add(transport_idx_t const& transport_idx_from,
                            std::uint32_t const stop_idx_from,
                            transport_idx_t const& transport_idx_to,
                            std::uint32_t const stop_idx_to,
                            bitfield_idx_t const& bitfield_idx,
                            day_idx_t const passes_midnight) {
  assert(!finalized_);
  assert(passes_midnight < 2U);

  if (!initialized_) {
    cur_transport_idx_from_ = transport_idx_from;
    cur_stop_idx_from_ = stop_idx_from;
    initialized_ = true;
  }

  if (stop_idx_from != cur_stop_idx_from_ ||
      transport_idx_from != cur_transport_idx_from_) {
    // finish current stop
    key key{cur_transport_idx_from_, cur_stop_idx_from_};
    entry entry{cur_start_, cur_start_ + cur_length_};
    index_.emplace(key, entry);

    // reset cursors
    cur_transport_idx_from_ = transport_idx_from;
    cur_stop_idx_from_ = stop_idx_from;
    cur_start_ += cur_length_;
    cur_length_ = 0U;
  }

  transfers_.emplace_back(bitfield_idx, transport_idx_to, stop_idx_to,
                          passes_midnight);
  ++cur_length_;
}

void hash_transfer_set::finalize() {
  key key{cur_transport_idx_from_, cur_stop_idx_from_};
  entry entry{cur_start_, cur_start_ + cur_length_};
  index_.emplace(key, entry);
  finalized_ = true;
}

std::optional<hash_transfer_set::entry> hash_transfer_set::get_transfers(
    transport_idx_t const& transport_idx_from, unsigned const stop_idx_from) {
  assert(finalized_);
  key key{transport_idx_from, stop_idx_from};
  return index_.contains(key)
             ? std::make_optional<hash_transfer_set::entry>(index_.at(key))
             : std::nullopt;
}

transport_segment_idx_t embed_day_offset(const day_idx_t base,
                                         const day_idx_t transport_day,
                                         const transport_idx_t transport_idx) {
  return (((static_cast<transport_segment_idx_t>(transport_day.v_) +
            QUERY_DAY_SHIFT) -
           static_cast<transport_segment_idx_t>(base.v_))
          << (32U - DAY_OFFSET_BITS)) |
         transport_idx.v_;
}
std::uint32_t day_offset(const transport_segment_idx_t transport_segment_idx) {
  return (transport_segment_idx & day_offset_mask) >> (32U - DAY_OFFSET_BITS);
}
day_idx_t transport_day(const day_idx_t base,
                        const transport_segment_idx_t transport_segment_idx) {
  return day_idx_t{base.v_ +
                   static_cast<int>(day_offset(transport_segment_idx)) -
                   QUERY_DAY_SHIFT};
}
transport_idx_t transport_idx(
    const transport_segment_idx_t transport_segment_idx) {
  return transport_idx_t{transport_segment_idx & transport_idx_mask};
}
}  // namespace nigiri::routing::tripbased