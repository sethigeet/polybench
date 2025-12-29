#pragma once
#include <cstdint>
#include <string>

enum class Side { Buy, Sell };
enum class Outcome { Yes, No };

struct OrderRequest {
  std::string market_id;
  Outcome outcome;
  double price;
  double quantity;
  Side side;
};

struct Order {
  std::string market_id;
  Outcome outcome;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  uint64_t timestamp;
};

struct FillReport {
  std::string market_id;
  Outcome outcome;
  uint64_t order_id;
  double filled_price;
  double filled_quantity;
  uint64_t timestamp;
  Side side;
};
