#include <benchmark/benchmark.h>

#include <unordered_map>

#include "trading/exchange.hpp"
#include "trading/market_book.hpp"
#include "types/common.hpp"

class ExchangeBenchmark : public benchmark::Fixture {
 public:
  Exchange exchange;
  std::unordered_map<MarketId, MarketBook> books;
  MarketId market_id = "test-market-benchmark";
  AssetId yes_asset = "yes-asset-bench";
  AssetId no_asset = "no-asset-bench";

  void SetUp(const ::benchmark::State&) override {
    auto& book = books[market_id];
    book.register_asset(yes_asset, Outcome::Yes);
    book.register_asset(no_asset, Outcome::No);
    exchange.set_books(&books);

    BookMessage yes_msg;
    yes_msg.asset_id = yes_asset;
    yes_msg.market = market_id;
    yes_msg.timestamp = 1000;
    for (int i = 0; i < 50; ++i) {
      yes_msg.bids.push_back({0.50 - i * 0.01, 100.0 + i * 10.0});
      yes_msg.asks.push_back({0.51 + i * 0.01, 100.0 + i * 10.0});
    }
    book.on_book_message(yes_msg);

    BookMessage no_msg;
    no_msg.asset_id = no_asset;
    no_msg.market = market_id;
    no_msg.timestamp = 1000;
    for (int i = 0; i < 50; ++i) {
      no_msg.bids.push_back({0.49 - i * 0.01, 100.0 + i * 10.0});
      no_msg.asks.push_back({0.50 + i * 0.01, 100.0 + i * 10.0});
    }
    book.on_book_message(no_msg);
  }
};

BENCHMARK_DEFINE_F(ExchangeBenchmark, BM_SubmitOrder_TakerFill)(benchmark::State& state) {
  for (auto _ : state) {
    Order order;
    order.market_id = market_id;
    order.outcome = Outcome::Yes;
    order.id = 1;
    order.price = 0.55;  // Crosses the ask
    order.quantity = 10.0;
    order.side = Side::Buy;
    order.timestamp = 1000;

    auto fill = exchange.submit_order(order);
    benchmark::DoNotOptimize(fill);
  }
}
BENCHMARK_REGISTER_F(ExchangeBenchmark, BM_SubmitOrder_TakerFill);

BENCHMARK_DEFINE_F(ExchangeBenchmark, BM_SubmitOrder_MakerQueue)(benchmark::State& state) {
  uint64_t order_id = 100;
  for (auto _ : state) {
    Order order;
    order.market_id = market_id;
    order.outcome = Outcome::Yes;
    order.id = order_id++;
    order.price = 0.45;  // Below best bid, won't fill
    order.quantity = 10.0;
    order.side = Side::Buy;
    order.timestamp = 1000;

    auto fill = exchange.submit_order(order);
    benchmark::DoNotOptimize(fill);

    // Clean up to avoid accumulating virtual orders
    exchange.cancel_order(market_id, order.id);
  }
}
BENCHMARK_REGISTER_F(ExchangeBenchmark, BM_SubmitOrder_MakerQueue);

class ExchangeWithVirtualOrdersBenchmark : public benchmark::Fixture {
 public:
  Exchange exchange;
  std::unordered_map<MarketId, MarketBook> books;
  MarketId market_id = "test-market-virtual";
  AssetId yes_asset = "yes-asset-virtual";
  AssetId no_asset = "no-asset-virtual";

  void SetUp(const ::benchmark::State&) override {
    auto& book = books[market_id];
    book.register_asset(yes_asset, Outcome::Yes);
    book.register_asset(no_asset, Outcome::No);
    exchange.set_books(&books);

    BookMessage yes_msg;
    yes_msg.asset_id = yes_asset;
    yes_msg.market = market_id;
    yes_msg.timestamp = 1000;
    for (int i = 0; i < 50; ++i) {
      yes_msg.bids.push_back({0.50 - i * 0.01, 100.0});
      yes_msg.asks.push_back({0.51 + i * 0.01, 100.0});
    }
    book.on_book_message(yes_msg);

    for (int i = 0; i < 20; ++i) {
      VirtualOrder vo;
      vo.market_id = market_id;
      vo.outcome = Outcome::Yes;
      vo.id = 1000 + i;
      vo.price = 0.55;
      vo.quantity = 10.0;
      vo.side = Side::Sell;
      vo.volume_ahead = 0.0;  // Will be filled immediately on trade
      vo.placed_at = 1000 + i;
      book.add_virtual_order(vo);
    }
  }

  void TearDown(const ::benchmark::State&) override {
    auto& book = books[market_id];
    auto& vos = book.get_virtual_orders(market_id);
    vos.clear();
    for (int i = 0; i < 20; ++i) {
      VirtualOrder vo;
      vo.market_id = market_id;
      vo.outcome = Outcome::Yes;
      vo.id = 1000 + i;
      vo.price = 0.55;
      vo.quantity = 10.0;
      vo.side = Side::Sell;
      vo.volume_ahead = 0.0;
      vo.placed_at = 1000 + i;
      book.add_virtual_order(vo);
    }
  }
};

BENCHMARK_DEFINE_F(ExchangeWithVirtualOrdersBenchmark, BM_ProcessTrade_WithVirtualOrders)
(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    auto& book = books[market_id];
    auto& vos = book.get_virtual_orders(market_id);
    vos.clear();
    for (int i = 0; i < 20; ++i) {
      VirtualOrder vo;
      vo.market_id = market_id;
      vo.outcome = Outcome::Yes;
      vo.id = 1000 + i;
      vo.price = 0.55;
      vo.quantity = 10.0;
      vo.side = Side::Sell;
      vo.volume_ahead = 0.0;
      vo.placed_at = 1000 + i;
      book.add_virtual_order(vo);
    }
    state.ResumeTiming();

    LastTradeMessage trade;
    trade.asset_id = yes_asset;
    trade.market = market_id;
    trade.price = 0.55;
    trade.side = Side::Buy;
    trade.size = 500.0;  // Large trade to fill all virtual orders
    trade.fee_rate_bps = 100;
    trade.timestamp = 2000;

    auto fills = exchange.process_trade(trade);
    benchmark::DoNotOptimize(fills);
  }
}
BENCHMARK_REGISTER_F(ExchangeWithVirtualOrdersBenchmark, BM_ProcessTrade_WithVirtualOrders);

BENCHMARK_DEFINE_F(ExchangeBenchmark, BM_ProcessTrade_NoVirtualOrders)(benchmark::State& state) {
  for (auto _ : state) {
    LastTradeMessage trade;
    trade.asset_id = yes_asset;
    trade.market = market_id;
    trade.price = 0.55;
    trade.side = Side::Buy;
    trade.size = 25.0;
    trade.fee_rate_bps = 100;
    trade.timestamp = 2000;

    auto fills = exchange.process_trade(trade);
    benchmark::DoNotOptimize(fills);
  }
}
BENCHMARK_REGISTER_F(ExchangeBenchmark, BM_ProcessTrade_NoVirtualOrders);
