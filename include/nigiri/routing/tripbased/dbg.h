#pragma once

#include "nigiri/types.h"

#define TBDL std::cout << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

static inline auto hhmmss(nigiri::duration_t const d) {
  return std::chrono::hh_mm_ss<nigiri::duration_t>{d};
}

static inline auto d_hhmmss(nigiri::duration_t const d) {
  auto d_remainder = d;
  nigiri::duration_t day{1440};
  unsigned days = 0;
  while (d_remainder > day) {
    ++days;
    d_remainder -= day;
  }
  return std::make_pair(days, hhmmss(d_remainder));
}