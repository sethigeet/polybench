#pragma once
#include <map>
#include <optional>
#include <vector>

#include "types.hpp"

struct PriceLevel {
  double price;
  double quantity;
};

class OrderBook {
 public:
  // Ticks represent Level 2 snapshots (total qty at price level)
  void on_tick(const MarketTick &tick);

  std::optional<double> get_best_bid() const noexcept;
  std::optional<double> get_best_ask() const noexcept;
  std::optional<double> get_mid_price() const noexcept;
  std::optional<double> get_spread() const noexcept;

  double get_bid_depth(double price) const noexcept;
  double get_ask_depth(double price) const noexcept;

  std::vector<PriceLevel> get_top_bids(size_t n) const;
  std::vector<PriceLevel> get_top_asks(size_t n) const;

  bool has_bids() const noexcept { return !bids_.empty(); }
  bool has_asks() const noexcept { return !asks_.empty(); }

  void clear() noexcept;

 private:
  // Bids: higher price is better, so we use reverse iteration
  // Price -> Qty (sorted ascending, access via rbegin)
  std::map<double, double> bids_;

  // Asks: lower price is better, so we use forward iteration
  // Price -> Qty (sorted ascending, access via begin)
  std::map<double, double> asks_;
};