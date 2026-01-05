#pragma once
#include <cstdint>

#include "common.hpp"

struct VirtualOrder {
  MarketId market_id;
  Outcome outcome;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  double volume_ahead;
  uint64_t placed_at;
};
