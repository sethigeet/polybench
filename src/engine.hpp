#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "exchange.hpp"
#include "market_book.hpp"
#include "portfolio_tracker.hpp"
#include "strategy.hpp"

class Engine {
 public:
  explicit Engine(std::shared_ptr<Strategy> strategy);
  void run();

 private:
  std::shared_ptr<Strategy> strategy_;
  std::unordered_map<std::string, MarketBook> books_;  // market_id -> MarketBook
  Exchange exchange_;
  PortfolioTracker portfolio_;
};
