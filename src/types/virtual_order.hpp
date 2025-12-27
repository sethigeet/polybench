#pragma once
#include <cstdint>
#include <string>

#include "common.hpp"

struct VirtualOrder {
  std::string market_id;
  Outcome outcome;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  double volume_ahead;
  uint64_t placed_at;
};
