#pragma once
#include <cstdint>

enum class Side { Buy, Sell };

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
