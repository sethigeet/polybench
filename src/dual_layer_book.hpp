#pragma once
#include <map>
#include <optional>
#include <vector>

#include "types/polymarket.hpp"
#include "types/virtual_order.hpp"

struct PriceLevel {
  double price;
  double quantity;
};

// Dual-Layer Order Book Architecture
// - Live Book: 1:1 mirror of Polymarket state, updated only by realtime market data messages
// - Strategy Book: Composite view with strategy's virtual orders overlaid
class DualLayerBook {
 public:
  // === Polymarket Book Management ===

  void on_book_message(const BookMessage& msg);
  void on_price_change(const PriceChangeMessage& msg);

  std::optional<double> get_live_best_bid() const noexcept;
  std::optional<double> get_live_best_ask() const noexcept;
  std::optional<double> get_live_mid_price() const noexcept;
  std::optional<double> get_live_spread() const noexcept;

  double get_live_bid_depth(double price) const noexcept;
  double get_live_ask_depth(double price) const noexcept;

  std::vector<PriceLevel> get_live_top_bids(size_t n) const;
  std::vector<PriceLevel> get_live_top_asks(size_t n) const;

  bool has_bids() const noexcept { return !live_bids_.empty(); }
  bool has_asks() const noexcept { return !live_asks_.empty(); }

  // === Virtual Order Management ===

  void add_virtual_order(const VirtualOrder& order);
  void remove_virtual_order(uint64_t order_id);
  std::vector<VirtualOrder>& get_virtual_orders() { return virtual_orders_; }
  const std::vector<VirtualOrder>& get_virtual_orders() const { return virtual_orders_; }

 private:
  std::map<double, double> live_bids_;  // Bids: higher price is better (access via rbegin)
  std::map<double, double> live_asks_;  // Asks: lower price is better (access via begin)

  std::vector<VirtualOrder> virtual_orders_;
};
