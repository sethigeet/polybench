#include <benchmark/benchmark.h>
#include <pybind11/embed.h>

#include "trading/strategy.hpp"
#include "types/common.hpp"
#include "types/polymarket.hpp"

namespace py = pybind11;

static const MarketId kMarketId(
    "market-pycb-00000000000000000000000000000000000000000000000000000");
static const AssetId kYesAsset(
    "yes-asset-pycb-00000000000000000000000000000000000000000000000000000000");

static py::scoped_interpreter *g_interp = nullptr;

struct StrategyHandle {
  py::object py_obj;
  std::shared_ptr<Strategy> ptr;
};

static void init_interpreter() {
  if (!g_interp) {
    g_interp = new py::scoped_interpreter{};
  }
}

static StrategyHandle make_noop_strategy() {
  py::exec(R"(
import polybench_core

class _NoopStrategy(polybench_core.Strategy):
    def on_book(self, msg):
        pass
    def on_price_change(self, msg):
        pass
    def on_trade(self, msg):
        pass
    def on_fill(self, fill):
        pass
)");
  py::object cls = py::module_::import("__main__").attr("_NoopStrategy");
  py::object obj = cls();
  return {obj, obj.cast<std::shared_ptr<Strategy>>()};
}

static StrategyHandle make_getter_strategy() {
  py::exec(R"(
import polybench_core

class _GetterStrategy(polybench_core.Strategy):
    def on_book(self, msg):
        self.get_outcome(msg.market, msg.asset_id)
        self.get_yes_best_bid(msg.market)
        self.get_yes_best_ask(msg.market)
    def on_price_change(self, msg):
        pass
    def on_trade(self, msg):
        pass
    def on_fill(self, fill):
        pass
)");
  py::object cls = py::module_::import("__main__").attr("_GetterStrategy");
  py::object obj = cls();
  return {obj, obj.cast<std::shared_ptr<Strategy>>()};
}

static BookMessage make_book_message() {
  BookMessage msg;
  msg.asset_id = kYesAsset;
  msg.market = kMarketId;
  msg.timestamp = 1000;
  for (int i = 0; i < 20; ++i) {
    msg.bids.push_back({0.50 - i * 0.01, 100.0 + i * 10.0});
    msg.asks.push_back({0.51 + i * 0.01, 100.0 + i * 10.0});
  }
  return msg;
}

static PriceChangeMessage make_price_change_message() {
  PriceChangeMessage msg;
  msg.market = kMarketId;
  msg.timestamp = 2000;
  PriceChange change;
  change.asset_id = kYesAsset;
  change.price = 0.50;
  change.size = 150.0;
  change.side = Side::Buy;
  change.best_bid = 0.50;
  change.best_ask = 0.51;
  msg.price_changes.push_back(change);
  return msg;
}

// Measures the round-trip cost of a single C++ -> Python -> return callback
static void BM_PythonCallback_OnBook_Noop(benchmark::State &state) {
  init_interpreter();
  auto handle = make_noop_strategy();
  auto msg = make_book_message();

  for (auto _ : state) {
    handle.ptr->on_book(msg);
  }
}
BENCHMARK(BM_PythonCallback_OnBook_Noop);

// Measures on_book callback that calls 3 getters back into C++
static void BM_PythonCallback_OnBook_WithGetters(benchmark::State &state) {
  init_interpreter();
  auto handle = make_getter_strategy();

  std::unordered_map<MarketId, MarketBook> books;
  books[kMarketId].register_asset(kYesAsset, Outcome::Yes);
  auto msg = make_book_message();
  books[kMarketId].on_book_message(msg);
  handle.ptr->set_books(&books);

  for (auto _ : state) {
    handle.ptr->on_book(msg);
  }
}
BENCHMARK(BM_PythonCallback_OnBook_WithGetters);

// Measures repeated book callbacks over a fixed-size batch
static void BM_PythonCallback_OnBookBatch(benchmark::State &state) {
  init_interpreter();
  auto handle = make_noop_strategy();

  constexpr size_t kBatchSize = 16;
  auto msg = make_book_message();

  for (auto _ : state) {
    for (size_t i = 0; i < kBatchSize; ++i) {
      handle.ptr->on_book(msg);
    }
  }
  state.SetItemsProcessed(state.iterations() * kBatchSize);
}
BENCHMARK(BM_PythonCallback_OnBookBatch);

// Measures on_price_change callback
static void BM_PythonCallback_OnPriceChange_Noop(benchmark::State &state) {
  init_interpreter();
  auto handle = make_noop_strategy();
  auto msg = make_price_change_message();

  for (auto _ : state) {
    handle.ptr->on_price_change(msg);
  }
}
BENCHMARK(BM_PythonCallback_OnPriceChange_Noop);

// Baseline: pure C++ strategy (no Python boundary crossing)
static void BM_CppCallback_OnBook_Baseline(benchmark::State &state) {
  class CppNoopStrategy : public Strategy {
   public:
    void on_book(const BookMessage &) override {}
    void on_price_change(const PriceChangeMessage &) override {}
    void on_trade(const LastTradeMessage &) override {}
    void on_fill(const FillReport &) override {}
  };

  auto strategy = std::make_shared<CppNoopStrategy>();
  auto msg = make_book_message();

  for (auto _ : state) {
    strategy->on_book(msg);
  }
}
BENCHMARK(BM_CppCallback_OnBook_Baseline);
