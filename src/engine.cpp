#include "engine.hpp"

#include <variant>
#include <vector>

#define LOGGER_NAME "Engine"
#include "logger.hpp"

Engine::Engine(std::shared_ptr<Strategy> strategy) : strategy_(strategy) {
  strategy_->set_engine_callbacks(
      [this](const Order& order) {
        auto fill = this->exchange_.submit_order(order);
        if (fill) {
          strategy_->on_fill(*fill);
          portfolio_.on_fill(*fill);
        }
      },
      [this](const std::string& market_id, uint64_t id) {
        this->exchange_.cancel_order(market_id, id);
      });

  strategy_->set_books(&books_);
  exchange_.set_books(&books_);
}

void Engine::run() {
  LOG_INFO("Starting simulation...");

  // === Synthetic Test Data ===
  const std::string market_id =
      "0xbd31dc8a20211944f6b70f31557f1001557b59905b7738480ca09bd4532f84af";

  // Register asset IDs with their outcomes
  books_[market_id].register_asset("yes_token_id", Outcome::Yes);
  books_[market_id].register_asset("no_token_id", Outcome::No);

  // Initial YES book snapshot
  BookMessage yes_snapshot;
  yes_snapshot.asset_id = "yes_token_id";
  yes_snapshot.market = market_id;
  yes_snapshot.timestamp = 1000;
  yes_snapshot.hash = "0x0...";
  yes_snapshot.bids = {
      {0.45, 100.0},
      {0.44, 200.0},
      {0.43, 150.0},
  };
  yes_snapshot.asks = {
      {0.46, 80.0},
      {0.47, 120.0},
      {0.48, 200.0},
  };

  // Initial NO book snapshot (complement prices)
  BookMessage no_snapshot;
  no_snapshot.asset_id = "no_token_id";
  no_snapshot.market = market_id;
  no_snapshot.timestamp = 1000;
  no_snapshot.hash = "0x0...";
  no_snapshot.bids = {
      {0.54, 80.0},   // Complement of YES ask 0.46
      {0.53, 120.0},  // Complement of YES ask 0.47
  };
  no_snapshot.asks = {
      {0.55, 100.0},  // Complement of YES bid 0.45
      {0.56, 200.0},  // Complement of YES bid 0.44
  };

  books_[market_id].on_book_message(yes_snapshot);
  books_[market_id].on_book_message(no_snapshot);

  auto& book = books_[market_id];
  LOG_DEBUG("Initial book snapshot applied for market {}.", market_id);
  LOG_DEBUG("\tYES: Best Bid: {}, Best Ask: {}", book.get_yes_best_bid().value_or(0),
            book.get_yes_best_ask().value_or(0));
  LOG_DEBUG("\tNO:  Best Bid: {}, Best Ask: {}", book.get_no_best_bid().value_or(0),
            book.get_no_best_ask().value_or(0));

  strategy_->on_book(yes_snapshot);
  strategy_->on_book(no_snapshot);

  // Polymarket API sends either a last_trade_price message OR a price_change message
  using MarketMessage = std::variant<PriceChangeMessage, LastTradeMessage>;

  std::vector<MarketMessage> messages = {
      // Trade: Someone buys YES - 30 shares traded at 0.46
      LastTradeMessage{"yes_token_id", market_id, 0.46, Side::Buy, 30.0, 0, 1001},

      // Price change: YES Ask depth at 0.46 reduced from 80 to 50
      PriceChangeMessage{
          market_id, {{"yes_token_id", 0.46, 50.0, Side::Sell, "hash1", 0.45, 0.46}}, 1001},

      // Trade: Someone buys NO at 0.54 (complement of YES 0.46) - creates YES+NO pair
      LastTradeMessage{"no_token_id", market_id, 0.54, Side::Buy, 20.0, 0, 1002},

      // Price change: NO Bid at 0.54 reduced from 80 to 60
      PriceChangeMessage{
          market_id, {{"no_token_id", 0.54, 60.0, Side::Buy, "hash2", 0.54, 0.55}}, 1002},

      // Trade: Someone sells YES at 0.45
      LastTradeMessage{"yes_token_id", market_id, 0.45, Side::Sell, 50.0, 0, 1003},

      // Price change: YES Bid at 0.45 reduced from 100 to 50
      PriceChangeMessage{
          market_id, {{"yes_token_id", 0.45, 50.0, Side::Buy, "hash3", 0.45, 0.46}}, 1003},
  };

  for (const auto& msg : messages) {
    std::visit(
        [this](auto&& message) {
          using T = std::decay_t<decltype(message)>;

          if constexpr (std::is_same_v<T, LastTradeMessage>) {
            auto fills = exchange_.process_trade(message);
            for (const auto& fill : fills) {
              strategy_->on_fill(fill);
              portfolio_.on_fill(fill);
            }
            strategy_->on_trade(message);

            // Update MTM prices from trade
            auto& book = books_[message.market];
            auto outcome_opt = book.get_outcome(message.asset_id);
            if (outcome_opt) {
              // Use best bid/ask midpoint for MTM
              if (*outcome_opt == Outcome::Yes) {
                auto bid = book.get_yes_best_bid();
                auto ask = book.get_yes_best_ask();
                if (bid && ask) {
                  portfolio_.update_mark_to_market(message.market, *outcome_opt, (*bid + *ask) / 2);
                } else {
                  portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                   bid.value_or(*ask));
                }
              } else {
                auto bid = book.get_no_best_bid();
                auto ask = book.get_no_best_ask();
                if (bid && ask) {
                  portfolio_.update_mark_to_market(message.market, *outcome_opt, (*bid + *ask) / 2);
                } else {
                  portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                   bid.value_or(*ask));
                }
              }
            }
          } else if constexpr (std::is_same_v<T, PriceChangeMessage>) {
            for (const auto& change : message.price_changes) {
              auto& book = books_[message.market];
              book.on_price_change(change);

              // Update MTM prices
              auto outcome_opt = book.get_outcome(change.asset_id);
              if (outcome_opt) {
                if (*outcome_opt == Outcome::Yes) {
                  auto bid = book.get_yes_best_bid();
                  auto ask = book.get_yes_best_ask();
                  if (bid && ask) {
                    portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                     (*bid + *ask) / 2);
                  } else {
                    portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                     bid.value_or(*ask));
                  }
                } else {
                  auto bid = book.get_no_best_bid();
                  auto ask = book.get_no_best_ask();
                  if (bid && ask) {
                    portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                     (*bid + *ask) / 2);
                  } else {
                    portfolio_.update_mark_to_market(message.market, *outcome_opt,
                                                     bid.value_or(*ask));
                  }
                }
              }
            }
            strategy_->on_price_change(message);
          }
        },
        msg);

    portfolio_.record_equity_snapshot();
  }

  // Print portfolio summary
  LOG_INFO("=== Portfolio Summary ===");
  LOG_INFO("Realized PnL: {:.4f}", portfolio_.get_realized_pnl());
  LOG_INFO("Unrealized PnL: {:.4f}", portfolio_.get_unrealized_pnl());
  LOG_INFO("Total PnL: {:.4f}", portfolio_.get_total_pnl());
  double sharpe = portfolio_.get_sharpe_ratio();
  if (sharpe != 0.0) {
    LOG_INFO("Sharpe Ratio: {:.4f}", sharpe);
  } else {
    LOG_INFO("Sharpe Ratio: N/A (insufficient data)");
  }

  std::ostringstream ss;
  ss << "Simulation finished. Final book states:\n";
  for (const auto& [market_id, market_book] : books_) {
    ss << "\tMarket: " << market_id << "\n";
    ss << "\t  YES: Best Bid: " << market_book.get_yes_best_bid().value_or(0)
       << " Best Ask: " << market_book.get_yes_best_ask().value_or(0) << "\n";
    ss << "\t  NO:  Best Bid: " << market_book.get_no_best_bid().value_or(0)
       << " Best Ask: " << market_book.get_no_best_ask().value_or(0) << "\n";
  }
  LOG_DEBUG(ss.str());
}
