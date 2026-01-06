#include "engine.hpp"

#include <chrono>
#include <sstream>
#include <thread>
#include <variant>

#include "types/fixed_string.hpp"

#define LOGGER_NAME "Engine"
#include "logger.hpp"

Engine::Engine(std::shared_ptr<Strategy> strategy, const EngineConfig& config)
    : config_(config), strategy_(strategy) {
  strategy_->set_engine_callbacks(
      [this](const Order& order) {
        auto fill = this->exchange_.submit_order(order);
        if (fill) {
          strategy_->on_fill(*fill);
          portfolio_.on_fill(*fill);
        }
      },
      [this](const MarketId& market_id, uint64_t id) {
        this->exchange_.cancel_order(market_id, id);
      });

  strategy_->set_books(&books_);
  exchange_.set_books(&books_);

  // Register asset mappings and track active markets
  for (const auto& [asset_id, mapping] : config_.asset_mappings) {
    books_[mapping.market_id].register_asset(asset_id, mapping.outcome);
    active_markets_.insert(mapping.market_id);
    LOG_DEBUG("Registered asset {} as {} for market {}", asset_id,
              mapping.outcome == Outcome::Yes ? "YES" : "NO", mapping.market_id);
  }
  LOG_INFO("Tracking {} active markets", active_markets_.size());

  WsConfig ws_config;
  ws_config.url = config_.ws_url;
  ws_config.asset_ids = config_.asset_ids;

  ws_ = std::make_unique<PolymarketWS>(ws_config);
  // ws_->on_connect([this]() { LOG_INFO("WebSocket connected - receiving live market data"); });
  // ws_->on_disconnect([this]() { LOG_WARN("WebSocket disconnected"); });
  // ws_->on_error([](const std::string& error) { LOG_ERROR("WebSocket error: {}", error); });
}

Engine::~Engine() { stop(); }

void Engine::run() {
  LOG_INFO("Subscribing to {} assets", config_.asset_ids.size());

  running_ = true;

  ws_->start();

  // Main event loop - poll messages from WebSocket and process them
  // This ensures Python callbacks run on the main thread, not the WS thread
  while (running_) {
    auto messages = ws_->poll_messages();

    if (messages.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    LOG_DEBUG("Polled {} messages from queue", messages.size());
    for (const auto& msg : messages) {
      process_message(msg);
    }
    portfolio_.record_equity_snapshot();
  }

  print_portfolio_summary();
}

void Engine::stop() {
  if (running_) {
    LOG_INFO("Stopping engine...");
    running_ = false;
    // NOTE: Don't call ws_->stop() here - it can cause a deadlock if called from
    // a signal handler while the main thread is holding queue_mutex_ in poll_messages().
    // The WebSocket will be cleaned up in the destructor after the main loop exits.
  }
}

void Engine::process_message(const PolymarketMessage& msg) {
  std::visit(
      [this](auto&& message) {
        using T = std::decay_t<decltype(message)>;

        if constexpr (std::is_same_v<T, BookMessage>) {
          process_book_message(message);
        } else if constexpr (std::is_same_v<T, PriceChangeMessage>) {
          process_price_change_message(message);
        } else if constexpr (std::is_same_v<T, LastTradeMessage>) {
          process_trade_message(message);
        } else if constexpr (std::is_same_v<T, TickSizeChangeMessage>) {
          process_tick_size_change_message(message);
        } else if constexpr (std::is_same_v<T, MarketResolvedMessage>) {
          process_market_resolved_message(message);
        }
      },
      msg);
}

void Engine::process_book_message(const BookMessage& msg) {
  LOG_DEBUG("Book snapshot for asset {} in market {}: {} bids, {} asks", msg.asset_id, msg.market,
            msg.bids.size(), msg.asks.size());

  auto it = books_.find(msg.market);
  if (it == books_.end()) {
    it = books_.emplace(msg.market, MarketBook{}).first;
  }
  auto& book = it->second;

  book.on_book_message(msg);
  strategy_->on_book(msg);
  update_mtm(msg.market, msg.asset_id);

  auto outcome = book.get_outcome(msg.asset_id);
  if (outcome) {
    if (*outcome == Outcome::Yes) {
      LOG_DEBUG("  YES: Best Bid: {}, Best Ask: {}", book.get_yes_best_bid().value_or(0),
                book.get_yes_best_ask().value_or(0));
    } else {
      LOG_DEBUG("  NO: Best Bid: {}, Best Ask: {}", book.get_no_best_bid().value_or(0),
                book.get_no_best_ask().value_or(0));
    }
  }
}

void Engine::process_price_change_message(const PriceChangeMessage& msg) {
  LOG_DEBUG("Price change in market {}: {} changes", msg.market, msg.price_changes.size());

  auto it = books_.find(msg.market);
  if (it == books_.end()) return;
  auto& book = it->second;

  for (const auto& change : msg.price_changes) {
    book.on_price_change(change);
    update_mtm(msg.market, change.asset_id);
  }

  strategy_->on_price_change(msg);
}

void Engine::process_trade_message(const LastTradeMessage& msg) {
  LOG_DEBUG("Trade: {} {} @ {} in market {}", msg.side == Side::Buy ? "BUY" : "SELL", msg.size,
            msg.price, msg.market);

  auto fills = exchange_.process_trade(msg);
  for (const auto& fill : fills) {
    strategy_->on_fill(fill);
    portfolio_.on_fill(fill);
  }

  strategy_->on_trade(msg);
  update_mtm(msg.market, msg.asset_id);
}

void Engine::process_tick_size_change_message(const TickSizeChangeMessage& msg) {
  LOG_INFO("Tick size change for asset {} in market {}: {} -> {}", msg.asset_id, msg.market,
           msg.old_tick_size, msg.new_tick_size);

  // NOTHING TO DO HERE
}

void Engine::process_market_resolved_message(const MarketResolvedMessage& msg) {
  LOG_INFO("Market resolved: {}", msg.market);
  LOG_INFO("  Winning outcome: {} (asset: {})", msg.winning_outcome == Outcome::Yes ? "UP" : "DOWN",
           msg.winning_asset_id);

  portfolio_.on_market_resolved(msg.market, msg.winning_outcome);
  strategy_->on_market_resolved(msg);

  if (!msg.asset_ids.empty()) {
    LOG_INFO("Unsubscribing from {} assets for resolved market", msg.asset_ids.size());
    ws_->unsubscribe(msg.asset_ids);
  }

  active_markets_.erase(msg.market);
  if (active_markets_.empty()) {
    LOG_INFO("All markets resolved - stopping simulation");
    stop();
  } else {
    LOG_INFO("Remaining active markets: {}", active_markets_.size());
  }
}

void Engine::update_mtm(const MarketId& market_id, const AssetId& asset_id) {
  auto it = books_.find(market_id);
  if (it == books_.end()) return;
  auto& book = it->second;

  auto outcome_opt = book.get_outcome(asset_id);
  if (!outcome_opt) return;

  double mtm_price = 0.0;

  if (*outcome_opt == Outcome::Yes) {
    auto bid = book.get_yes_best_bid();
    auto ask = book.get_yes_best_ask();
    if (bid && ask) {
      mtm_price = (*bid + *ask) / 2.0;
    } else if (bid) {
      mtm_price = *bid;
    } else if (ask) {
      mtm_price = *ask;
    }
  } else {
    auto bid = book.get_no_best_bid();
    auto ask = book.get_no_best_ask();
    if (bid && ask) {
      mtm_price = (*bid + *ask) / 2.0;
    } else if (bid) {
      mtm_price = *bid;
    } else if (ask) {
      mtm_price = *ask;
    }
  }

  if (mtm_price > 0) {
    portfolio_.update_mark_to_market(market_id, *outcome_opt, mtm_price);
  }
}

void Engine::print_portfolio_summary() {
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
  ss << "Final book states:\n";
  for (const auto& [mid, market_book] : books_) {
    ss << "\tMarket: " << mid << "\n";
    ss << "\t  YES: Best Bid: " << market_book.get_yes_best_bid().value_or(0)
       << " Best Ask: " << market_book.get_yes_best_ask().value_or(0) << "\n";
    ss << "\t  NO:  Best Bid: " << market_book.get_no_best_bid().value_or(0)
       << " Best Ask: " << market_book.get_no_best_ask().value_or(0) << "\n";
  }
  LOG_DEBUG("{}", ss.str());
}
