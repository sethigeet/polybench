#pragma once
#include <cstdint>
#include <string>

enum class Side { Bid, Ask };

struct MarketTick {
  uint64_t timestamp;
  double price;
  double quantity;
  Side side;
};

struct Order {
  uint64_t id;
  double price;
  double quantity;
  Side side;
  uint64_t timestamp;
};

struct FillReport {
  uint64_t order_id;
  double filled_price;
  double filled_quantity;
  uint64_t timestamp;
};
