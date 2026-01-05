#pragma once
#include <cstdint>

#include "fixed_string.hpp"

enum class Side { Buy, Sell };
enum class Outcome { Yes, No };

using MarketId = FixedString<66>;
using AssetId = FixedString<77>;

struct OrderRequest {
  MarketId market_id;
  Outcome outcome;
  double price;
  double quantity;
  Side side;
};

struct Order {
  MarketId market_id;
  Outcome outcome;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  uint64_t timestamp;
};

struct FillReport {
  MarketId market_id;
  Outcome outcome;
  uint64_t order_id;
  double filled_price;
  double filled_quantity;
  uint64_t timestamp;
  Side side;
};
