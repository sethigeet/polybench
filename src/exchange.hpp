#pragma once
#include <optional>
#include <stdexcept>
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
  void set_book(DualLayerBook* book) { book_ = book; }

  void submit_order(const Order& order);
  void cancel_order(uint64_t order_id);

  std::vector<FillReport> process_price_change(const PriceChangeMessage& msg);

  static bool is_valid_price(double price) noexcept {
    return price >= polymarket::MIN_PRICE && price <= polymarket::MAX_PRICE;
  }

 private:
  DualLayerBook* book_ = nullptr;

  std::optional<FillReport> try_fill_taker(const Order& order, uint64_t timestamp);
  void add_maker_order(const Order& order);
  std::vector<FillReport> process_virtual_fills(double price, Side side, double reduction,
                                                uint64_t timestamp);
};
