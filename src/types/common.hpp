#pragma once
#include <cstdint>
#include <string>

enum class Side { Buy, Sell };

struct OrderRequest {
  std::string asset_id;
  double price;
  double quantity;
  Side side;
};

struct Order {
  std::string asset_id;
  uint64_t id;
  double price;
  double quantity;
  Side side;
  uint64_t timestamp;
};

struct FillReport {
  std::string asset_id;
  uint64_t order_id;
  double filled_price;
  double filled_quantity;
  uint64_t timestamp;
};
