#pragma once
#include <cstdint>

#include "common.hpp"
#include "small_vector.hpp"

struct OrderSummary {
  double price;
  double size;
};

using OrderList = SmallVector<OrderSummary, 20>;

// Book snapshot message (book event)
// Emitted on first subscription and when a trade affects the book
struct BookMessage {
  AssetId asset_id;
  MarketId market;
  OrderList bids;
  OrderList asks;
  uint64_t timestamp;
};

// Single price change within a price_change message
struct PriceChange {
  AssetId asset_id;
  double price;
  double size;  // New total size at this price level
  Side side;
  double best_bid;
  double best_ask;
};

using PriceChangeList = SmallVector<PriceChange, 2>;

// price_change message
// Emitted when a new order is placed or an order is cancelled
struct PriceChangeMessage {
  MarketId market;  // Condition ID
  PriceChangeList price_changes;
  uint64_t timestamp;
};

// last_trade_price message
// Emitted when a maker and taker order is matched
struct LastTradeMessage {
  AssetId asset_id;
  MarketId market;
  double price;
  Side side;
  double size;
  int fee_rate_bps;
  uint64_t timestamp;
};

// tick_size_change message
// Emitted when price reaches limits (>0.96 or <0.04)
struct TickSizeChangeMessage {
  AssetId asset_id;
  MarketId market;
  double old_tick_size;
  double new_tick_size;
  uint64_t timestamp;
};

// market_resolved message
// Emitted when a market is resolved
struct MarketResolvedMessage {
  MarketId market;
  AssetId winning_asset_id;
  Outcome winning_outcome;
  SmallVector<AssetId, 2> asset_ids;
  uint64_t timestamp;
};
