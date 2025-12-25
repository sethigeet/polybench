#pragma once
#include <memory>

#include "mock_exchange.hpp"
#include "order_book.hpp"
#include "strategy.hpp"

class Engine {
 public:
  Engine(std::shared_ptr<Strategy> strategy);
  void run();

 private:
  std::shared_ptr<Strategy> strategy_;
  MockExchange exchange_;
  OrderBook book_;
};
