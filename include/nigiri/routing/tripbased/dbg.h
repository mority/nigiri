#pragma once

#include "nigiri/types.h"

#define TBDL std::cout << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

static inline auto hhmmss(nigiri::duration_t const d) {
  return std::chrono::hh_mm_ss<nigiri::duration_t>{d};
}