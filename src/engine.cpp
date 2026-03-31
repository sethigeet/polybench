#include "engine.hpp"

#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <variant>

#include "polymarket_ws.hpp"
#include "types/fixed_string.hpp"
#include "utils/thread.hpp"

#define LOGGER_NAME "Engine"
#include "logger.hpp"

namespace {

struct ActiveEngineHandle {
  void* instance = nullptr;
  void (*stop)(void*) = nullptr;
};

ActiveEngineHandle g_active_engine;

struct ActiveEngineScope {
  explicit ActiveEngineScope(ActiveEngineHandle handle) { g_active_engine = handle; }
  ~ActiveEngineScope() { g_active_engine = {}; }
};

TransportConfig make_transport_config(const EngineConfig& config) {
  TransportConfig transport_config;
  transport_config.mode = config.runtime.mode;
  transport_config.url = config.ws_url;
  transport_config.asset_ids = config.asset_ids;
  transport_config.message_queue_capacity = config.runtime.message_queue_capacity;
  transport_config.consumer_spin_count = config.runtime.consumer_spin_count;
  transport_config.consumer_wait_timeout_us = config.runtime.consumer_wait_timeout_us;
  transport_config.consumer_sleep_initial_us = config.runtime.consumer_sleep_initial_us;
  transport_config.consumer_sleep_max_us = config.runtime.consumer_sleep_max_us;
  transport_config.ingest_cpu_affinity = config.runtime.ingest_cpu_affinity;
  transport_config.engine_cpu_affinity = config.runtime.engine_cpu_affinity;
  transport_config.socket_rcvbuf_bytes = config.runtime.socket_rcvbuf_bytes;
  transport_config.busy_poll_us = config.runtime.busy_poll_us;
  transport_config.recv_batch_size = config.runtime.recv_batch_size;
  transport_config.perf_stats = config.runtime.perf_stats;
  return transport_config;
}

template <typename Transport>
class Engine {
 public:
  Engine(std::shared_ptr<Strategy> strategy, const EngineConfig& config)
      : config_(config),
        strategy_(std::move(strategy)),
        transport_(make_transport_config(config_)) {
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

    for (const auto& [asset_id, mapping] : config_.asset_mappings) {
      books_[mapping.market_id].register_asset(asset_id, mapping.outcome);
      active_markets_.insert(mapping.market_id);
      LOG_DEBUG("Registered asset {} as {} for market {}", asset_id,
                mapping.outcome == Outcome::Yes ? "YES" : "NO", mapping.market_id);
    }
    LOG_INFO("Tracking {} active markets", active_markets_.size());

    transport_.on_connect([]() { LOG_INFO("Market data transport connected"); });
    transport_.on_disconnect([]() { LOG_INFO("Market data transport disconnected"); });
    transport_.on_error(
        [](const std::string& error) { LOG_ERROR("Market data transport error: {}", error); });
  }

  ~Engine() { stop(); }

  void run() {
    LOG_INFO("Subscribing to {} assets", config_.asset_ids.size());

    running_ = true;

    if (config_.runtime.engine_cpu_affinity >= 0) {
      utils::thread::pin_current_thread_to_cpu(config_.runtime.engine_cpu_affinity, "engine");
    }

    transport_.start();

    auto current_sleep_us = config_.runtime.consumer_sleep_initial_us;
    SmallVector<PolymarketMessage, kMessageBatchSize> messages;
    while (running_) {
      messages.clear();
      transport_.poll_messages(messages, kMessageBatchSize);

      if (messages.empty()) {
        bool received = false;
        for (int i = 0; i < config_.runtime.consumer_spin_count && running_; ++i) {
          std::this_thread::yield();
          transport_.poll_messages(messages, kMessageBatchSize);
          if (!messages.empty()) {
            received = true;
            break;
          }
        }

        if (!received && running_) {
          received = transport_.wait_for_messages(
              std::chrono::microseconds(config_.runtime.consumer_wait_timeout_us));
        }

        if (!received && running_) {
          std::this_thread::sleep_for(std::chrono::microseconds(current_sleep_us));
          current_sleep_us = std::min(current_sleep_us * 2, config_.runtime.consumer_sleep_max_us);
        }

        if (messages.empty()) {
          continue;
        }
      }

      current_sleep_us = config_.runtime.consumer_sleep_initial_us;
      const auto dispatch_start = std::chrono::steady_clock::now();
      for (const auto& msg : messages) {
        process_message(msg);
      }
      auto& perf_stats = const_cast<PerfStats&>(transport_.perf_stats());
      const auto dispatch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - dispatch_start)
                                   .count();
      perf_stats.record_engine_dispatch(messages.size(), dispatch_ns);
      perf_stats.maybe_log(LOGGER_NAME);
      portfolio_.record_equity_snapshot();
    }

    print_portfolio_summary();
  }

  void stop() {
    if (running_) {
      LOG_INFO("Stopping engine...");
      running_ = false;
      transport_.stop();
    }
  }

 private:
  void process_message(const PolymarketMessage& msg) {
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

  void process_book_message(const BookMessage& msg) {
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

  void process_price_change_message(const PriceChangeMessage& msg) {
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

  void process_trade_message(const LastTradeMessage& msg) {
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

  void process_tick_size_change_message(const TickSizeChangeMessage& msg) {
    LOG_INFO("Tick size change for asset {} in market {}: {} -> {}", msg.asset_id, msg.market,
             msg.old_tick_size, msg.new_tick_size);
  }

  void process_market_resolved_message(const MarketResolvedMessage& msg) {
    LOG_INFO("Market resolved: {}", msg.market);
    LOG_INFO("  Winning outcome: {} (asset: {})",
             msg.winning_outcome == Outcome::Yes ? "UP" : "DOWN", msg.winning_asset_id);

    portfolio_.on_market_resolved(msg.market, msg.winning_outcome);
    strategy_->on_market_resolved(msg);

    if (!msg.asset_ids.empty()) {
      LOG_INFO("Unsubscribing from {} assets for resolved market", msg.asset_ids.size());
      transport_.unsubscribe(msg.asset_ids);
    }

    active_markets_.erase(msg.market);
    if (active_markets_.empty()) {
      LOG_INFO("All markets resolved - stopping simulation");
      stop();
    } else {
      LOG_INFO("Remaining active markets: {}", active_markets_.size());
    }
  }

  void update_mtm(const MarketId& market_id, const AssetId& asset_id) {
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

  void print_portfolio_summary() {
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

  EngineConfig config_;
  std::shared_ptr<Strategy> strategy_;
  std::unordered_map<MarketId, MarketBook> books_;
  std::unordered_set<MarketId> active_markets_;
  Exchange exchange_;
  PortfolioTracker portfolio_;
  Transport transport_;
  std::atomic<bool> running_{false};
};

template <typename Transport>
void stop_engine_instance(void* instance) {
  static_cast<Engine<Transport>*>(instance)->stop();
}

template <typename Transport>
int run_engine_impl(std::shared_ptr<Strategy> strategy, const EngineConfig& config) {
  Engine<Transport> engine(std::move(strategy), config);
  ActiveEngineScope active_engine{
      ActiveEngineHandle{.instance = &engine, .stop = &stop_engine_instance<Transport>}};
  engine.run();
  return 0;
}

}  // namespace

int run_engine(std::shared_ptr<Strategy> strategy, const EngineConfig& config) {
  switch (config.runtime.mode) {
    case TransportMode::IxWebSocket:
      return run_engine_impl<PolymarketWS>(std::move(strategy), config);
  }

  throw std::runtime_error("Unknown transport mode");
}

void stop_active_engine() {
  if (g_active_engine.instance != nullptr && g_active_engine.stop != nullptr) {
    g_active_engine.stop(g_active_engine.instance);
  }
}
