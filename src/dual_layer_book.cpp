#include "dual_layer_book.hpp"

#include <algorithm>

void DualLayerBook::on_book_message(const BookMessage& msg) {
  live_bids_.clear();
  live_asks_.clear();

  for (const auto& bid : msg.bids) {
    if (bid.size > 0) {
      live_bids_[bid.price] = bid.size;
    }
  }

  for (const auto& ask : msg.asks) {
    if (ask.size > 0) {
      live_asks_[ask.price] = ask.size;
    }
  }
}

void DualLayerBook::on_price_change(const PriceChangeMessage& msg) {
  for (const auto& change : msg.price_changes) {
    auto& book = (change.side == Side::Buy) ? live_bids_ : live_asks_;

    if (change.size <= 0) {
      book.erase(change.price);
    } else {
      book[change.price] = change.size;
    }
  }
}

std::optional<double> DualLayerBook::get_live_best_bid() const noexcept {
  if (live_bids_.empty()) return std::nullopt;
  return live_bids_.rbegin()->first;  // Highest bid price
}

std::optional<double> DualLayerBook::get_live_best_ask() const noexcept {
  if (live_asks_.empty()) return std::nullopt;
  return live_asks_.begin()->first;  // Lowest ask price
}

std::optional<double> DualLayerBook::get_live_mid_price() const noexcept {
  auto best_bid = get_live_best_bid();
  auto best_ask = get_live_best_ask();
  if (!best_bid || !best_ask) return std::nullopt;
  return (*best_bid + *best_ask) / 2.0;
}

std::optional<double> DualLayerBook::get_live_spread() const noexcept {
  auto best_bid = get_live_best_bid();
  auto best_ask = get_live_best_ask();
  if (!best_bid || !best_ask) return std::nullopt;
  return *best_ask - *best_bid;
}

double DualLayerBook::get_live_bid_depth(double price) const noexcept {
  auto it = live_bids_.find(price);
  return it != live_bids_.end() ? it->second : 0.0;
}

double DualLayerBook::get_live_ask_depth(double price) const noexcept {
  auto it = live_asks_.find(price);
  return it != live_asks_.end() ? it->second : 0.0;
}

std::vector<PriceLevel> DualLayerBook::get_live_top_bids(size_t n) const {
  std::vector<PriceLevel> result;
  result.reserve(n);
  size_t count = 0;
  for (auto it = live_bids_.rbegin(); it != live_bids_.rend() && count < n; ++it, ++count) {
    result.push_back({it->first, it->second});
  }
  return result;
}

std::vector<PriceLevel> DualLayerBook::get_live_top_asks(size_t n) const {
  std::vector<PriceLevel> result;
  result.reserve(n);
  size_t count = 0;
  for (auto it = live_asks_.begin(); it != live_asks_.end() && count < n; ++it, ++count) {
    result.push_back({it->first, it->second});
  }
  return result;
}

void DualLayerBook::add_virtual_order(const VirtualOrder& order) {
  virtual_orders_.push_back(order);
}

void DualLayerBook::remove_virtual_order(uint64_t order_id) {
  virtual_orders_.erase(
      std::remove_if(virtual_orders_.begin(), virtual_orders_.end(),
                     [order_id](const VirtualOrder& o) { return o.id == order_id; }),
      virtual_orders_.end());
}
