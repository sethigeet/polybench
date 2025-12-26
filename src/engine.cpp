#include "engine.hpp"

#include <variant>
#include <vector>

#define LOGGER_NAME "Engine"
#include "logger.hpp"

Engine::Engine(std::shared_ptr<Strategy> strategy) : strategy_(strategy) {
  strategy_->set_engine_callbacks(
      [this](const Order& order) { this->exchange_.submit_order(order); },
      [this](uint64_t id) { this->exchange_.cancel_order(id); });

  strategy_->set_book(&book_);
  exchange_.set_book(&book_);
}

void Engine::run() {
  LOG_INFO("Starting simulation...");

  // === Synthetic Test Data ===
  BookMessage initial_snapshot;
  initial_snapshot.asset_id =
      "65818619657568813474341868652308942079804919287380422192892211131408793125422";
  initial_snapshot.market = "0xbd31dc8a20211944f6b70f31557f1001557b59905b7738480ca09bd4532f84af";
  initial_snapshot.timestamp = 1000;
  initial_snapshot.hash = "0x0...";
  initial_snapshot.bids = {
      {0.45, 100.0},
      {0.44, 200.0},
      {0.43, 150.0},
  };
  initial_snapshot.asks = {
      {0.46, 80.0},
      {0.47, 120.0},
      {0.48, 200.0},
  };

  // Apply initial book snapshot
  book_.on_book_message(initial_snapshot);
  LOG_INFO("Initial book snapshot applied. Best Bid: {}, Best Ask: {}",
           book_.get_live_best_bid().value_or(0), book_.get_live_best_ask().value_or(0));

  // Notify strategy of initial book state
  strategy_->on_book(initial_snapshot);

  // Polymarket API sends either a last_trade_price message OR a price_change message
  // These are independent events, not bundled together
  using MarketMessage = std::variant<PriceChangeMessage, LastTradeMessage>;

  std::vector<MarketMessage> messages = {
      // Trade: Someone lifts the ask - 30 shares traded at 0.46
      LastTradeMessage{initial_snapshot.asset_id, "0xbd31dc...", 0.46, Side::Buy, 30.0, 0, 1001},

      // Price change: Ask depth at 0.46 reduced from 80 to 50 (reflects the trade above)
      PriceChangeMessage{"0xbd31dc...",
                         {{initial_snapshot.asset_id, 0.46, 50.0, Side::Sell, "hash1", 0.45, 0.46}},
                         1001},

      // Trade: Someone hits the bid - 50 shares traded at 0.45
      LastTradeMessage{initial_snapshot.asset_id, "0xbd31dc...", 0.45, Side::Sell, 50.0, 0, 1002},

      // Price change: Bid depth at 0.45 reduced from 100 to 50
      PriceChangeMessage{"0xbd31dc...",
                         {{initial_snapshot.asset_id, 0.45, 50.0, Side::Buy, "hash2", 0.45, 0.46}},
                         1002},

      // Price change: New ask at 0.455 (order placement, no trade)
      PriceChangeMessage{
          "0xbd31dc...",
          {{initial_snapshot.asset_id, 0.455, 25.0, Side::Sell, "hash3", 0.45, 0.455}},
          1003},

      // Trade: Aggressive buyer clears the 0.455 ask
      LastTradeMessage{initial_snapshot.asset_id, "0xbd31dc...", 0.455, Side::Buy, 25.0, 0, 1004},

      // Price change: 0.455 level now empty
      PriceChangeMessage{"0xbd31dc...",
                         {{initial_snapshot.asset_id, 0.455, 0.0, Side::Sell, "hash4", 0.45, 0.46}},
                         1004},

      // Trade: More buying at 0.46
      LastTradeMessage{initial_snapshot.asset_id, "0xbd31dc...", 0.46, Side::Buy, 20.0, 0, 1005},

      // Price change: Ask at 0.46 reduced to 30
      PriceChangeMessage{"0xbd31dc...",
                         {{initial_snapshot.asset_id, 0.46, 30.0, Side::Sell, "hash5", 0.45, 0.46}},
                         1005},

      // Trade: Bid side gets hit
      LastTradeMessage{initial_snapshot.asset_id, "0xbd31dc...", 0.45, Side::Sell, 50.0, 0, 1006},

      // Price change: Bid at 0.45 now empty, best bid becomes 0.44
      PriceChangeMessage{"0xbd31dc...",
                         {{initial_snapshot.asset_id, 0.45, 0.0, Side::Buy, "hash6", 0.44, 0.46}},
                         1006},
  };

  for (const auto& msg : messages) {
    std::visit(
        [this](auto&& message) {
          using T = std::decay_t<decltype(message)>;

          if constexpr (std::is_same_v<T, LastTradeMessage>) {
            auto fills = exchange_.process_trade(message);
            for (const auto& fill : fills) {
              strategy_->on_fill(fill);
            }
            strategy_->on_trade(message);
          } else if constexpr (std::is_same_v<T, PriceChangeMessage>) {
            book_.on_price_change(message);
            strategy_->on_price_change(message);
          }
        },
        msg);
  }

  LOG_INFO("Simulation finished. Final Best Bid: {}, Best Ask: {}",
           book_.get_live_best_bid().value_or(0), book_.get_live_best_ask().value_or(0));
}