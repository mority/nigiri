#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "nigiri/loader/gtfs/agency.h"
#include "nigiri/types.h"

namespace nigiri::loader::gtfs {

struct route {
  provider_idx_t agency_;
  std::string id_;
  std::string short_name_;
  std::string long_name_;
  std::string desc_;
  clasz clasz_;
};

using route_map_t = hash_map<std::string, std::unique_ptr<route>>;

route_map_t read_routes(agency_map_t const&, std::string_view file_content);

}  // namespace nigiri::loader::gtfs