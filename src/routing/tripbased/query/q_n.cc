#include "nigiri/routing/tripbased/query/q_n.h"
#include "nigiri/routing/tripbased/dbg.h"
#include "nigiri/routing/tripbased/query/transport_segment.h"
#include "nigiri/routing/tripbased/settings.h"

using namespace nigiri;
using namespace nigiri::routing::tripbased;

void q_n::reset(day_idx_t new_base) {
#ifndef NDEBUG
  TBDL << "Resetting transport segment queue\n";
#endif
  base_ = new_base;
  start_.clear();
  start_.emplace_back(0U);
  end_.clear();
  end_.emplace_back(0U);
  segments_.clear();
}

#ifdef TB_MIN_WALK
void q_n::enqueue_walk(std::uint16_t const transport_day,
                       transport_idx_t const transport_idx,
                       std::uint16_t const stop_idx,
                       std::uint16_t const n_transfers,
                       std::uint16_t const time_walk,
                       std::uint32_t const transferred_from) {
  assert(segments_.size() < std::numeric_limits<queue_idx_t>::max());
  assert(base_.has_value());

  auto const day_offset = compute_day_offset(base_->v_, transport_day);
  // query day has offset = 5, we disregard segments with offset > 6, since we
  // are only interested in journeys with max. 24h travel time
  if (0 <= day_offset && day_offset < 7) {

    // compute transport segment index
    auto const transport_segment_idx =
        embed_day_offset(day_offset, transport_idx);

    // look-up the minimum total walking time with which the stop index is
    // reached
    auto const reached_time_walk =
        r_.walk(transport_segment_idx, n_transfers, stop_idx);
    if (time_walk < reached_time_walk) {

      // new n?
      if (n_transfers == start_.size()) {
        start_.emplace_back(segments_.size());
        end_.emplace_back(segments_.size());
      }

      // find the subsequent stop with lower or equal total walking time
      auto const reached_stop_idx =
          r_.stop(transport_segment_idx, n_transfers, time_walk);

      // add transport segment
#ifdef TB_CACHE_PRESSURE_REDUCTION
      segments_.emplace_back(transport_segment_idx, stop_idx, reached_stop_idx,
                             transferred_from, time_walk);
#else
      segments_.emplace_back(transport_segment_idx, stop_idx, reached_stop_idx,
                             transferred_from);
#endif
#ifndef NDEBUG
      TBDL << "Enqueued transport segment: ";
      print(std::cout, static_cast<queue_idx_t>(segments_.size() - 1));
#endif

      // increment index
      ++end_[n_transfers];

      // update reached
      r_.update(transport_segment_idx, stop_idx, n_transfers, time_walk);
    }
  }
}

#elifdef TB_TRANSFER_CLASS

void q_n::enqueue_class(std::uint16_t const transport_day,
                        transport_idx_t const transport_idx,
                        std::uint16_t const stop_idx,
                        std::uint16_t const n_transfers,
                        std::uint8_t const transfer_class_max,
                        std::uint8_t const transfer_class_sum,
                        std::uint32_t const transferred_from) {
  assert(segments_.size() < std::numeric_limits<queue_idx_t>::max());
  assert(base_.has_value());

  auto const day_offset = compute_day_offset(base_->v_, transport_day);
  // query day has offset = 5, we disregard segments with offset > 6, since we
  // are only interested in journeys with max. 24h travel time
  if (0 <= day_offset && day_offset < 7) {

    // compute transport segment index
    auto const transport_segment_idx =
        embed_day_offset(day_offset, transport_idx);

    // look-up the transfer class criteria with which the stop index is reached
    auto const reached_transfer_class =
        r_.transfer_class(transport_segment_idx, n_transfers, stop_idx);
    auto const reached_transfer_class_max = reached_transfer_class.first;
    auto const reached_transfer_class_sum = reached_transfer_class.second;
    if (transfer_class_max < reached_transfer_class_max ||
        transfer_class_sum < reached_transfer_class_sum) {

      // new n?
      if (n_transfers == start_.size()) {
        start_.emplace_back(segments_.size());
        end_.emplace_back(segments_.size());
      }

      // find the subsequent stop with lower or equal transfer class criteria
      auto const reached_stop_idx =
          r_.stop(transport_segment_idx, n_transfers, transfer_class_max,
                  transfer_class_sum);

      // add transport segment
#ifdef TB_CACHE_PRESSURE_REDUCTION
      segments_.emplace_back(transport_segment_idx, stop_idx, reached_stop_idx,
                             transferred_from, transfer_class_max,
                             transfer_class_sum);
#else
      segments_.emplace_back(transport_segment_idx, stop_idx, reached_stop_idx,
                             transferred_from);
#endif

#ifndef NDEBUG
      TBDL << "Enqueued transport segment: ";
      print(std::cout, static_cast<queue_idx_t>(segments_.size() - 1));
#endif

      // increment index
      ++end_[n_transfers];

      // update reached
      r_.update(transport_segment_idx, stop_idx, n_transfers,
                transfer_class_max, transfer_class_sum);
    }
  }
}

#else
void q_n::enqueue(std::uint16_t const transport_day,
                  transport_idx_t const transport_idx,
                  std::uint16_t const stop_idx,
                  std::uint16_t const n_transfers,
                  std::uint32_t const transferred_from) {
  assert(segments_.size() < std::numeric_limits<queue_idx_t>::max());
  assert(base_.has_value());

  auto const day_offset = compute_day_offset(base_->v_, transport_day);
  // query day has offset = 5, we disregard segments with offset > 6, since we
  // are only interested in journeys with max. 24h travel time
  if (0 <= day_offset && day_offset < 7) {

    // compute transport segment index
    auto const transport_segment_idx =
        embed_day_offset(day_offset, transport_idx);

    // look-up the earliest stop index reached
    auto const r_query_res = r_.query(transport_segment_idx, n_transfers);
    if (stop_idx < r_query_res) {

      // new n?
      if (n_transfers == start_.size()) {
        start_.emplace_back(segments_.size());
        end_.emplace_back(segments_.size());
      }

      // add transport segment
      segments_.emplace_back(transport_segment_idx, stop_idx, r_query_res,
                             transferred_from);
#ifndef NDEBUG
      TBDL << "Enqueued transport segment: ";
      print(std::cout, static_cast<queue_idx_t>(segments_.size() - 1));
#endif

      // increment index
      ++end_[n_transfers];

      // update reached
      r_.update(transport_segment_idx, stop_idx, n_transfers);
    }
  }
}
#endif

void q_n::print(std::ostream& out, queue_idx_t const q_idx) {
  out << "q_idx: " << std::to_string(q_idx) << ", segment of ";
  segments_[q_idx].print(out, r_.tt_);
}