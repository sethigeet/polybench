#include "order_book.hpp"

#include "types.hpp"

void OrderBook::on_tick(const MarketTick &tick) {
  if (tick.side == Side::Bid) {
    if (tick.quantity <= 0) {
      bids_.erase(tick.price);
    } else {
      bids_[tick.price] = tick.quantity;
    }
  } else {
    if (tick.quantity <= 0) {
      asks_.erase(tick.price);
    } else {
      asks_[tick.price] = tick.quantity;
    }
  }
}

std::optional<double> OrderBook::get_best_bid() const noexcept {
  if (bids_.empty()) return std::nullopt;
  return bids_.rbegin()->first;  // Highest bid price
}

std::optional<double> OrderBook::get_best_ask() const noexcept {
  if (asks_.empty()) return std::nullopt;
  return asks_.begin()->first;  // Lowest ask price
}

std::optional<double> OrderBook::get_mid_price() const noexcept {
  auto best_bid = get_best_bid();
  auto best_ask = get_best_ask();
  if (!best_bid || !best_ask) return std::nullopt;
  return (*best_bid + *best_ask) / 2.0;
}

std::optional<double> OrderBook::get_spread() const noexcept {
  auto best_bid = get_best_bid();
  auto best_ask = get_best_ask();
  if (!best_bid || !best_ask) return std::nullopt;
  return *best_ask - *best_bid;
}

double OrderBook::get_bid_depth(double price) const noexcept {
  auto it = bids_.find(price);
  return it != bids_.end() ? it->second : 0.0;
}

double OrderBook::get_ask_depth(double price) const noexcept {
  auto it = asks_.find(price);
  return it != asks_.end() ? it->second : 0.0;
}

std::vector<PriceLevel> OrderBook::get_top_bids(size_t n) const {
  std::vector<PriceLevel> result;
  result.reserve(n);
  size_t count = 0;
  // Iterate from highest to lowest bid
  for (auto it = bids_.rbegin(); it != bids_.rend() && count < n; ++it, ++count) {
    result.push_back({it->first, it->second});
  }
  return result;
}

std::vector<PriceLevel> OrderBook::get_top_asks(size_t n) const {
  std::vector<PriceLevel> result;
  result.reserve(n);
  size_t count = 0;
  // Iterate from lowest to highest ask
  for (auto it = asks_.begin(); it != asks_.end() && count < n; ++it, ++count) {
    result.push_back({it->first, it->second});
  }
  return result;
}

void OrderBook::clear() noexcept {
  bids_.clear();
  asks_.clear();
}
