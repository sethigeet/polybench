#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "dual_layer_book.hpp"
#include "types/common.hpp"

namespace polymarket {
constexpr double MIN_PRICE = 0.01;
constexpr double MAX_PRICE = 0.99;
}  // namespace polymarket

class PriceValidationError : public std::runtime_error {
 public:
  explicit PriceValidationError(const std::string& msg) : std::runtime_error(msg) {}
};

class Exchange {
 public:
  void set_books(std::unordered_map<std::string, DualLayerBook>* books) { books_ = books; }

  void submit_order(const Order& order);
  void cancel_order(const std::string& asset_id, uint64_t order_id);

  std::vector<FillReport> process_trade(const LastTradeMessage& trade);

  static bool is_valid_price(double price) noexcept {
    return price >= polymarket::MIN_PRICE && price <= polymarket::MAX_PRICE;
  }

 private:
  std::unordered_map<std::string, DualLayerBook>* books_ = nullptr;

  DualLayerBook* get_book(const std::string& asset_id);

  std::optional<FillReport> try_fill_taker(const Order& order, uint64_t timestamp);
  void add_maker_order(const Order& order);
  std::vector<FillReport> process_virtual_fills(const std::string& asset_id, double price, Side side,
                                                double trade_size, uint64_t timestamp);
};
