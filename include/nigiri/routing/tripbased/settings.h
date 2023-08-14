#pragma once

// preprocessing options
#define TB_PREPRO_LB_PRUNING
#ifndef TB_PREPRO_LB_PRUNING
#define TB_PREPRO_VANILLA
#endif
#define TB_PREPRO_UTURN_REMOVAL
#define TB_PREPRO_TRANSFER_REDUCTION

// additional criteria
// #define TB_MIN_WALK
// #define TB_TRANSFER_CLASS
#ifdef TB_TRANSFER_CLASS
#define TB_TRANSFER_CLASS0 15
#define TB_TRANSFER_CLASS1 5
#endif

// system limits - number of bits
#define BITFIELD_IDX_BITS 25U
#define TRANSPORT_IDX_BITS 26U
#define STOP_IDX_BITS 10U
#define DAY_IDX_BITS 9U
#define NUM_TRANSFERS_BITS 5U
#define DAY_OFFSET_BITS 3U

// the position of the query day in the day offset
#define QUERY_DAY_SHIFT 5

namespace nigiri::routing::tripbased {

constexpr unsigned const kBitfieldIdxMax = 1U << BITFIELD_IDX_BITS;
constexpr unsigned const kTransportIdxMax = 1U << TRANSPORT_IDX_BITS;
constexpr unsigned const kStopIdxMax = 1U << STOP_IDX_BITS;
constexpr unsigned const kDayIdxMax = 1U << DAY_IDX_BITS;
constexpr unsigned const kNumTransfersMax = 1U << NUM_TRANSFERS_BITS;
constexpr unsigned const kDayOffsetMax = 1U << DAY_OFFSET_BITS;

}  // namespace nigiri::routing::tripbased