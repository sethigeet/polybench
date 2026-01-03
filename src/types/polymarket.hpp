#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"

struct OrderSummary {
  double price;
  double size;
};

// Book snapshot message (book event)
// Emitted on first subscription and when a trade affects the book
struct BookMessage {
  std::string asset_id;  // ERC1155 token ID
  std::string market;    // Condition ID (hex)
  std::vector<OrderSummary> bids;
  std::vector<OrderSummary> asks;
  uint64_t timestamp;
};

// Single price change within a price_change message
struct PriceChange {
  std::string asset_id;
  double price;
  double size;  // New total size at this price level
  Side side;
  double best_bid;
  double best_ask;
};

// price_change message
// Emitted when a new order is placed or an order is cancelled
struct PriceChangeMessage {
  std::string market;  // Condition ID
  std::vector<PriceChange> price_changes;
  uint64_t timestamp;
};

// last_trade_price message
// Emitted when a maker and taker order is matched
struct LastTradeMessage {
  std::string asset_id;
  std::string market;
  double price;
  Side side;
  double size;
  int fee_rate_bps;
  uint64_t timestamp;
};

// tick_size_change message
// Emitted when price reaches limits (>0.96 or <0.04)
struct TickSizeChangeMessage {
  std::string asset_id;
  std::string market;
  double old_tick_size;
  double new_tick_size;
  uint64_t timestamp;
};
