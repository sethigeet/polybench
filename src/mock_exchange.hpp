#pragma once
#include <iostream>
#include <map>
#include <vector>

#include "order_book.hpp"
#include "types.hpp"

class MockExchange {
 public:
  void set_order_book(const OrderBook *book) { order_book_ = book; }

  void submit_order(const Order &order);
  void cancel_order(uint64_t order_id);

  std::vector<FillReport> match(const MarketTick &tick);

 private:
  std::map<uint64_t, Order> orders_;
  const OrderBook *order_book_ = nullptr;

  double calculate_fill_price(const Order &order, const MarketTick &tick) const;
};
