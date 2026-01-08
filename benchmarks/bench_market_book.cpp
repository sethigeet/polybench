#include <benchmark/benchmark.h>

#include "market_book.hpp"
#include "types/common.hpp"

static const MarketId kMarketId(
    "market-bench-00000000000000000000000000000000000000000000000000000");
static const AssetId kYesAsset(
    "yes-asset-book-bench-00000000000000000000000000000000000000000000000000000000");
static const AssetId kNoAsset(
    "no-asset-book-bench-000000000000000000000000000000000000000000000000000000000");

class MarketBookBenchmark : public benchmark::Fixture {
 public:
  MarketBook book;

  void SetUp(const ::benchmark::State&) override {
    book.register_asset(kYesAsset, Outcome::Yes);
    book.register_asset(kNoAsset, Outcome::No);

    BookMessage yes_msg;
    yes_msg.asset_id = kYesAsset;
    yes_msg.market = kMarketId;
    yes_msg.timestamp = 1000;
    for (int i = 0; i < 50; ++i) {
      yes_msg.bids.push_back({0.50 - i * 0.01, 100.0 + i * 10.0});
      yes_msg.asks.push_back({0.51 + i * 0.01, 100.0 + i * 10.0});
    }
    book.on_book_message(yes_msg);

    BookMessage no_msg;
    no_msg.asset_id = kNoAsset;
    no_msg.market = kMarketId;
    no_msg.timestamp = 1000;
    for (int i = 0; i < 50; ++i) {
      no_msg.bids.push_back({0.49 - i * 0.01, 100.0 + i * 10.0});
      no_msg.asks.push_back({0.50 + i * 0.01, 100.0 + i * 10.0});
    }
    book.on_book_message(no_msg);

    for (int i = 0; i < 20; ++i) {
      VirtualOrder vo;
      vo.market_id = kMarketId;
      vo.outcome = Outcome::Yes;
      vo.id = 1000 + i;
      vo.price = 0.55 + i * 0.01;
      vo.quantity = 10.0;
      vo.side = Side::Sell;
      vo.volume_ahead = 50.0 + i * 5.0;
      vo.placed_at = 1000 + i;
      book.add_virtual_order(vo);
    }
  }
};

BENCHMARK_DEFINE_F(MarketBookBenchmark, BM_OnBookMessage)(benchmark::State& state) {
  BookMessage msg;
  msg.asset_id = kYesAsset;
  msg.market = kMarketId;
  msg.timestamp = 2000;

  for (int i = 0; i < 40; ++i) {
    msg.bids.push_back({0.50 - i * 0.01, 100.0 + i * 5.0});
    msg.asks.push_back({0.51 + i * 0.01, 100.0 + i * 5.0});
  }

  for (auto _ : state) {
    book.on_book_message(msg);
    benchmark::DoNotOptimize(book.get_yes_best_bid());
  }
}
BENCHMARK_REGISTER_F(MarketBookBenchmark, BM_OnBookMessage);

BENCHMARK_DEFINE_F(MarketBookBenchmark, BM_OnPriceChange)(benchmark::State& state) {
  double price = 0.45;
  for (auto _ : state) {
    PriceChange change;
    change.asset_id = kYesAsset;
    change.price = price;
    change.size = 150.0;
    change.side = Side::Buy;
    change.best_bid = 0.50;
    change.best_ask = 0.51;

    book.on_price_change(change);
    benchmark::DoNotOptimize(book.get_yes_bid_depth(price));

    // Toggle price to avoid just overwriting same level
    price = (price == 0.45) ? 0.46 : 0.45;
  }
}
BENCHMARK_REGISTER_F(MarketBookBenchmark, BM_OnPriceChange);

BENCHMARK_DEFINE_F(MarketBookBenchmark, BM_GetBestBidAsk)(benchmark::State& state) {
  for (auto _ : state) {
    auto yes_bid = book.get_yes_best_bid();
    auto yes_ask = book.get_yes_best_ask();
    auto no_bid = book.get_no_best_bid();
    auto no_ask = book.get_no_best_ask();
    benchmark::DoNotOptimize(yes_bid);
    benchmark::DoNotOptimize(yes_ask);
    benchmark::DoNotOptimize(no_bid);
    benchmark::DoNotOptimize(no_ask);
  }
}
BENCHMARK_REGISTER_F(MarketBookBenchmark, BM_GetBestBidAsk);

BENCHMARK_DEFINE_F(MarketBookBenchmark, BM_GetDepth)(benchmark::State& state) {
  for (auto _ : state) {
    auto d1 = book.get_yes_bid_depth(0.50);
    auto d2 = book.get_yes_ask_depth(0.51);
    auto d3 = book.get_no_bid_depth(0.49);
    auto d4 = book.get_no_ask_depth(0.50);
    benchmark::DoNotOptimize(d1);
    benchmark::DoNotOptimize(d2);
    benchmark::DoNotOptimize(d3);
    benchmark::DoNotOptimize(d4);
  }
}
BENCHMARK_REGISTER_F(MarketBookBenchmark, BM_GetDepth);

BENCHMARK_DEFINE_F(MarketBookBenchmark, BM_AddRemoveVirtualOrder)(benchmark::State& state) {
  uint64_t id = 10000;
  for (auto _ : state) {
    VirtualOrder vo;
    vo.market_id = kMarketId;
    vo.outcome = Outcome::Yes;
    vo.id = id++;
    vo.price = 0.55;
    vo.quantity = 10.0;
    vo.side = Side::Sell;
    vo.volume_ahead = 50.0;
    vo.placed_at = 5000;

    book.add_virtual_order(vo);
    book.remove_virtual_order(kMarketId, vo.id);
  }
}
BENCHMARK_REGISTER_F(MarketBookBenchmark, BM_AddRemoveVirtualOrder);
