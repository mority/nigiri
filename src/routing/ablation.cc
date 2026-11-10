#include "nigiri/routing/ablation.h"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>

namespace nigiri::routing {

namespace {

[[noreturn]] void invalid(std::string_view const var, std::string_view const s) {
  std::cerr << "invalid " << var << ": \"" << s << "\"\n";
  std::abort();
}

}  // namespace

ablation parse_ablation(std::string_view const s) {
  auto a = ablation{};
  auto rest = s;
  while (!rest.empty()) {
    auto const end = rest.find('+');
    auto const token = rest.substr(0U, end);
    if (token == "no-sharing") {
      a.no_sharing_ = true;
    } else if (token == "no-pingbound") {
      a.no_ping_bound_ = true;
    } else if (token == "no-skip") {
      a.no_skip_ = true;
    } else if (token == "no-limit") {
      a.no_limit_ = true;
    } else if (token == "no-lb") {
      a.no_lb_ = true;
    } else if (token == "no-routelb") {
      a.no_route_lb_ = true;
    } else if (token == "no-lazyreset") {
      a.no_lazy_reset_ = true;
    } else {
      invalid("NIGIRI_ABLATION", s);
    }
    if (end == std::string_view::npos) {
      break;
    }
    rest.remove_prefix(end + 1U);
    if (rest.empty()) {
      invalid("NIGIRI_ABLATION", s);  // trailing "+"
    }
  }
  return a;
}

ablation const& get_ablation() {
  static auto const a = []() {
    auto const* env = std::getenv("NIGIRI_ABLATION");
    return parse_ablation(env == nullptr ? std::string_view{}
                                         : std::string_view{env});
  }();
  return a;
}

duration_t get_pong_max_lookahead() {
  static auto const d = []() {
    constexpr auto const kDefaultDays = 6;
    auto const* env = std::getenv("NIGIRI_PONG_LOOKAHEAD_DAYS");
    if (env == nullptr || *env == '\0') {
      return duration_t{kDefaultDays * 1440};
    }
    auto const s = std::string_view{env};
    auto days = 0;
    auto const [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), days);
    // At most 14 days: departures and arrivals have to stay within the 16 bit
    // range of delta_t around the base day (about 22 days).
    if (ec != std::errc{} || ptr != s.data() + s.size() || days < 1 ||
        days > 14) {
      invalid("NIGIRI_PONG_LOOKAHEAD_DAYS", s);
    }
    return duration_t{days * 1440};
  }();
  return d;
}

}  // namespace nigiri::routing
