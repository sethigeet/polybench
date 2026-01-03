#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "exchange.hpp"
#include "market_book.hpp"
#include "polymarket_ws.hpp"
#include "portfolio_tracker.hpp"
#include "strategy.hpp"

struct EngineConfig {
  std::string ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
  std::vector<std::string> asset_ids;

  struct AssetMapping {
    std::string market_id;
    Outcome outcome;
  };
  std::unordered_map<std::string, AssetMapping> asset_mappings;
};

class Engine {
 public:
  explicit Engine(std::shared_ptr<Strategy> strategy, const EngineConfig& config);
  ~Engine();

  void run();
  void stop();

 private:
  void process_message(const PolymarketMessage& msg);
  void process_book_message(const BookMessage& msg);
  void process_price_change_message(const PriceChangeMessage& msg);
  void process_trade_message(const LastTradeMessage& msg);
  void process_tick_size_change_message(const TickSizeChangeMessage& msg);
  void process_market_resolved_message(const MarketResolvedMessage& msg);
  void update_mtm(const std::string& market_id, const std::string& asset_id);
  void print_portfolio_summary();

  EngineConfig config_;
  std::shared_ptr<Strategy> strategy_;
  std::unordered_map<std::string, MarketBook> books_;
  std::unordered_set<std::string> active_markets_;
  Exchange exchange_;
  PortfolioTracker portfolio_;
  std::unique_ptr<PolymarketWS> ws_;
  std::atomic<bool> running_{false};
};
