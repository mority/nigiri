#pragma once

#include "nigiri/types.h"
#include <format>

#define TBDL std::cout << "[" << __FILE_NAME__ << ":" << __LINE__ << "] "

static inline std::string dhhmm(nigiri::duration_t const& d) {
  nigiri::duration_t rem = d;
  unsigned days = 0;
  while (rem >= nigiri::duration_t{1440}) {
    rem -= nigiri::duration_t{1440};
    ++days;
  }
  unsigned hours = 0;
  while (rem >= nigiri::duration_t{60}) {
    rem -= nigiri::duration_t{60};
    ++hours;
  }
  std::string result = std::to_string(days) + "d" +
                       fmt::format("{:0>2}", hours) +
                       fmt::format("{:0>2}", rem.count()) + "h";
  return result;
}