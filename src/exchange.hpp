#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "market_book.hpp"
#include "types/common.hpp"

namespace polymarket {
constexpr double MIN_PRICE = 0.01;
constexpr double MAX_PRICE = 0.99;
constexpr double COMPLEMENT_BASE = 1.0;

inline double complement_price(double price) noexcept { return COMPLEMENT_BASE - price; }

inline bool is_valid_price(double price) noexcept {
  return price >= MIN_PRICE && price <= MAX_PRICE;
}
}  // namespace polymarket

class PriceValidationError : public std::runtime_error {
 public:
  explicit PriceValidationError(const std::string& msg) : std::runtime_error(msg) {}
};

class Exchange {
 public:
  void set_books(std::unordered_map<MarketId, MarketBook>* books) { books_ = books; }

  std::optional<FillReport> submit_order(const Order& order);
  void cancel_order(const MarketId& market_id, uint64_t order_id);

  std::vector<FillReport> process_trade(const LastTradeMessage& trade);

 private:
  std::unordered_map<MarketId, MarketBook>* books_ = nullptr;

  MarketBook* get_book(const MarketId& market_id);

  std::optional<FillReport> try_fill_taker(const Order& order);
  void add_maker_order(const Order& order);
  std::vector<FillReport> process_virtual_fills(const MarketId& market_id, Outcome outcome,
                                                double price, Side side, double trade_size,
                                                uint64_t timestamp);
};
