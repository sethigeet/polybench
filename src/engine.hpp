#pragma once
#include <memory>
#include <string>
#include <unordered_map>

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
  std::unordered_map<std::string, DualLayerBook> books_;
};
