#include "engine.hpp"

#include <vector>

#define LOGGER_NAME "Engine"
#include "logger.hpp"

Engine::Engine(std::shared_ptr<Strategy> strategy) : strategy_(strategy) {
  strategy_->set_engine_callbacks(
      [this](const Order &order) { this->exchange_.submit_order(order); },
      [this](uint64_t id) { this->exchange_.cancel_order(id); });

  strategy_->set_order_book(&book_);
  exchange_.set_order_book(&book_);
}

void Engine::run() {
  LOG_INFO("Starting simulation...");

  // Generate some dummy data for simulation
  // These represent Level 2 market data updates (total qty at each price level)
  std::vector<MarketTick> ticks = {{100, 100.0, 10, Side::Ask}, {101, 100.5, 5, Side::Ask},
                                   {102, 99.5, 10, Side::Bid},  {103, 101.0, 20, Side::Ask},
                                   {104, 100.0, 5, Side::Bid},  {105, 99.0, 10, Side::Bid}};

  for (const auto &tick : ticks) {
    // NOTE: We need to ensure that we update the book with market data first,
    // then fill all the possible orders and then let the strategy know about
    // the tick and read the order book as it is safe to read now

    book_.on_tick(tick);

    auto fills = exchange_.match(tick);
    for (const auto &fill : fills) {
      strategy_->on_fill(fill);
    }

    strategy_->on_tick(tick);
  }

  LOG_INFO("Simulation finished.");
}