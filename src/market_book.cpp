#include "market_book.hpp"

#include <algorithm>
#include <unordered_set>

void MarketBook::register_asset(AssetId asset_id, Outcome outcome) {
  asset_outcomes_[asset_id] = outcome;
}

std::optional<Outcome> MarketBook::get_outcome(const AssetId& asset_id) const {
  auto it = asset_outcomes_.find(asset_id);
  if (it == asset_outcomes_.end()) return std::nullopt;
  return it->second;
}

void MarketBook::on_book_message(const BookMessage& msg) {
  auto outcome_opt = get_outcome(msg.asset_id);
  if (!outcome_opt) return;
  Outcome outcome = *outcome_opt;

  auto& bids = (outcome == Outcome::Yes) ? yes_bids_ : no_bids_;
  auto& asks = (outcome == Outcome::Yes) ? yes_asks_ : no_asks_;

  bids.clear();
  asks.clear();

  for (const auto& bid : msg.bids) {
    bids[bid.price] = bid.size;
  }

  for (const auto& ask : msg.asks) {
    asks[ask.price] = ask.size;
  }
}

void MarketBook::on_price_change(const PriceChange& change) {
  auto outcome_opt = get_outcome(change.asset_id);
  if (!outcome_opt) return;
  Outcome outcome = *outcome_opt;

  auto& book = (change.side == Side::Buy) ? ((outcome == Outcome::Yes) ? yes_bids_ : no_bids_)
                                          : ((outcome == Outcome::Yes) ? yes_asks_ : no_asks_);

  if (change.size <= 0) {
    book.erase(change.price);
  } else {
    book[change.price] = change.size;
  }
}

std::optional<double> MarketBook::get_yes_best_bid() const noexcept {
  if (yes_bids_.empty()) return std::nullopt;
  return yes_bids_.rbegin()->first;
}

std::optional<double> MarketBook::get_yes_best_ask() const noexcept {
  if (yes_asks_.empty()) return std::nullopt;
  return yes_asks_.begin()->first;
}

double MarketBook::get_yes_bid_depth(double price) const noexcept {
  auto it = yes_bids_.find(price);
  return it != yes_bids_.end() ? it->second : 0.0;
}

double MarketBook::get_yes_ask_depth(double price) const noexcept {
  auto it = yes_asks_.find(price);
  return it != yes_asks_.end() ? it->second : 0.0;
}

std::optional<double> MarketBook::get_no_best_bid() const noexcept {
  if (no_bids_.empty()) return std::nullopt;
  return no_bids_.rbegin()->first;
}

std::optional<double> MarketBook::get_no_best_ask() const noexcept {
  if (no_asks_.empty()) return std::nullopt;
  return no_asks_.begin()->first;
}

double MarketBook::get_no_bid_depth(double price) const noexcept {
  auto it = no_bids_.find(price);
  return it != no_bids_.end() ? it->second : 0.0;
}

double MarketBook::get_no_ask_depth(double price) const noexcept {
  auto it = no_asks_.find(price);
  return it != no_asks_.end() ? it->second : 0.0;
}

void MarketBook::add_virtual_order(const VirtualOrder& order) {
  virtual_orders_[order.market_id].push_back(order);
}

void MarketBook::remove_virtual_order(const MarketId& market_id, uint64_t order_id) {
  auto it = virtual_orders_.find(market_id);
  if (it == virtual_orders_.end()) return;

  auto& orders = it->second;
  orders.erase(std::remove_if(orders.begin(), orders.end(),
                              [order_id](const VirtualOrder& o) { return o.id == order_id; }),
               orders.end());
}

void MarketBook::remove_virtual_orders(const MarketId& market_id,
                                       const std::vector<uint64_t>& order_ids) {
  auto it = virtual_orders_.find(market_id);
  if (it == virtual_orders_.end()) return;

  std::unordered_set<uint64_t> ids_to_remove(order_ids.begin(), order_ids.end());

  auto& orders = it->second;
  orders.erase(std::remove_if(orders.begin(), orders.end(),
                              [&ids_to_remove](const VirtualOrder& o) {
                                return ids_to_remove.count(o.id) > 0;
                              }),
               orders.end());
}

std::vector<VirtualOrder>& MarketBook::get_virtual_orders(const MarketId& market_id) {
  return virtual_orders_[market_id];
}

const std::vector<VirtualOrder>* MarketBook::get_virtual_orders(const MarketId& market_id) const {
  auto it = virtual_orders_.find(market_id);
  if (it == virtual_orders_.end()) return nullptr;
  return &it->second;
}

bool MarketBook::has_virtual_orders(const MarketId& market_id) const {
  auto it = virtual_orders_.find(market_id);
  return it != virtual_orders_.end() && !it->second.empty();
}
