#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types/common.hpp"
#include "types/polymarket.hpp"
#include "types/virtual_order.hpp"

// 4-sided order book for Polymarket markets
// Each market has: YES bids, YES asks, NO bids, NO asks
// Matching: BUY YES@p matches SELL YES@p OR BUY NO@(1-p) and so on...
class MarketBook {
 public:
  void register_asset(AssetId asset_id, Outcome outcome);
  std::optional<Outcome> get_outcome(const AssetId& asset_id) const;

  void on_book_message(const BookMessage& msg);
  void on_price_change(const PriceChange& change);

  std::optional<double> get_yes_best_bid() const noexcept;
  std::optional<double> get_yes_best_ask() const noexcept;
  double get_yes_bid_depth(double price) const noexcept;
  double get_yes_ask_depth(double price) const noexcept;

  std::optional<double> get_no_best_bid() const noexcept;
  std::optional<double> get_no_best_ask() const noexcept;
  double get_no_bid_depth(double price) const noexcept;
  double get_no_ask_depth(double price) const noexcept;

  void add_virtual_order(const VirtualOrder& order);
  void remove_virtual_order(const MarketId& market_id, uint64_t order_id);
  void remove_virtual_orders(const MarketId& market_id, std::span<const uint64_t> order_ids);
  std::vector<VirtualOrder>& get_virtual_orders(const MarketId& market_id);
  const std::vector<VirtualOrder>* get_virtual_orders(const MarketId& market_id) const;
  bool has_virtual_orders(const MarketId& market_id) const;

 private:
  std::unordered_map<AssetId, Outcome> asset_outcomes_;

  // Price level: (price, quantity)
  using PriceLevel = std::pair<double, double>;

  // YES outcome order book
  std::vector<PriceLevel> yes_bids_;  // Sorted descending by price (best bid at front)
  std::vector<PriceLevel> yes_asks_;  // Sorted ascending by price (best ask at front)

  // NO outcome order book
  std::vector<PriceLevel> no_bids_;  // Sorted descending by price
  std::vector<PriceLevel> no_asks_;  // Sorted ascending by price

  static auto find_level(std::vector<PriceLevel>& levels, double price) {
    return std::find_if(levels.begin(), levels.end(),
                        [price](const PriceLevel& l) { return l.first == price; });
  }

  static auto find_level(const std::vector<PriceLevel>& levels, double price) {
    return std::find_if(levels.begin(), levels.end(),
                        [price](const PriceLevel& l) { return l.first == price; });
  }

  std::unordered_map<MarketId, std::vector<VirtualOrder>> virtual_orders_;
};
