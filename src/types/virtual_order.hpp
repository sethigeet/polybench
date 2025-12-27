#pragma once
#include <cstdint>
#include <string>

#include "common.hpp"

struct VirtualOrder {
  std::string asset_id;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  double volume_ahead;
  uint64_t placed_at;
};
