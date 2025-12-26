#pragma once
#include <memory>

#include "dual_layer_book.hpp"
#include "exchange.hpp"
#include "strategy.hpp"

class Engine {
 public:
  Engine(std::shared_ptr<Strategy> strategy);
  void run();

 private:
  std::shared_ptr<Strategy> strategy_;
  Exchange exchange_;
  DualLayerBook book_;
};
