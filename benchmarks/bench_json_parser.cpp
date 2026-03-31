#include <benchmark/benchmark.h>

#include "json_parser.hpp"

static const std::string kBookMessageJson = R"({
  "event_type": "book",
  "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836455",
  "market": "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
  "timestamp": "1704067200000000",
  "bids": [
    {"price": "0.53", "size": "100.5"},
    {"price": "0.52", "size": "200.0"},
    {"price": "0.51", "size": "150.0"},
    {"price": "0.50", "size": "300.0"},
    {"price": "0.49", "size": "250.0"},
    {"price": "0.48", "size": "175.0"},
    {"price": "0.47", "size": "225.0"},
    {"price": "0.46", "size": "180.0"},
    {"price": "0.45", "size": "320.0"},
    {"price": "0.44", "size": "280.0"},
    {"price": "0.43", "size": "190.0"},
    {"price": "0.42", "size": "210.0"},
    {"price": "0.41", "size": "165.0"},
    {"price": "0.40", "size": "195.0"},
    {"price": "0.39", "size": "240.0"},
    {"price": "0.38", "size": "155.0"},
    {"price": "0.37", "size": "185.0"},
    {"price": "0.36", "size": "275.0"},
    {"price": "0.35", "size": "145.0"},
    {"price": "0.34", "size": "215.0"}
  ],
  "asks": [
    {"price": "0.55", "size": "120.0"},
    {"price": "0.56", "size": "180.0"},
    {"price": "0.57", "size": "220.0"},
    {"price": "0.58", "size": "160.0"},
    {"price": "0.59", "size": "190.0"},
    {"price": "0.60", "size": "250.0"},
    {"price": "0.61", "size": "170.0"},
    {"price": "0.62", "size": "140.0"},
    {"price": "0.63", "size": "200.0"},
    {"price": "0.64", "size": "230.0"},
    {"price": "0.65", "size": "185.0"},
    {"price": "0.66", "size": "175.0"},
    {"price": "0.67", "size": "155.0"},
    {"price": "0.68", "size": "195.0"},
    {"price": "0.69", "size": "210.0"},
    {"price": "0.70", "size": "165.0"},
    {"price": "0.71", "size": "145.0"},
    {"price": "0.72", "size": "225.0"},
    {"price": "0.73", "size": "135.0"},
    {"price": "0.74", "size": "205.0"}
  ]
})";

static const std::string kPriceChangeMessageJson = R"({
  "event_type": "price_change",
  "market": "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
  "timestamp": "1704067200000001",
  "price_changes": [
    {
      "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836455",
      "price": "0.54",
      "size": "150.0",
      "side": "buy",
      "best_bid": "0.54",
      "best_ask": "0.55"
    },
    {
      "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836456",
      "price": "0.46",
      "size": "120.0",
      "side": "sell",
      "best_bid": "0.45",
      "best_ask": "0.46"
    }
  ]
})";

static const std::string kLastTradeMessageJson = R"({
  "event_type": "last_trade_price",
  "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836455",
  "market": "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
  "price": "0.55",
  "side": "buy",
  "size": "25.5",
  "fee_rate_bps": "100",
  "timestamp": "1704067200000002"
})";

static void BM_ParseBookMessage(benchmark::State& state) {
  JsonParser parser;
  SmallVector<PolymarketMessage, 16> messages;
  for (auto _ : state) {
    messages.clear();
    parser.parse(kBookMessageJson, messages);
    benchmark::DoNotOptimize(messages);
  }
}
BENCHMARK(BM_ParseBookMessage);

static void BM_ParsePriceChangeMessage(benchmark::State& state) {
  JsonParser parser;
  SmallVector<PolymarketMessage, 16> messages;
  for (auto _ : state) {
    messages.clear();
    parser.parse(kPriceChangeMessageJson, messages);
    benchmark::DoNotOptimize(messages);
  }
}
BENCHMARK(BM_ParsePriceChangeMessage);

static void BM_ParseLastTradeMessage(benchmark::State& state) {
  JsonParser parser;
  SmallVector<PolymarketMessage, 16> messages;
  for (auto _ : state) {
    messages.clear();
    parser.parse(kLastTradeMessageJson, messages);
    benchmark::DoNotOptimize(messages);
  }
}
BENCHMARK(BM_ParseLastTradeMessage);

static std::string create_batch_json() {
  return "[" + kBookMessageJson + "," + kPriceChangeMessageJson + "," + kLastTradeMessageJson +
         "," + kPriceChangeMessageJson + "," + kLastTradeMessageJson + "," +
         kPriceChangeMessageJson + "," + kLastTradeMessageJson + "," + kPriceChangeMessageJson +
         "," + kLastTradeMessageJson + "," + kPriceChangeMessageJson + "]";
}

static void BM_ParseBatchMessages(benchmark::State& state) {
  JsonParser parser;
  const std::string batch = create_batch_json();
  SmallVector<PolymarketMessage, 16> messages;
  for (auto _ : state) {
    messages.clear();
    parser.parse(batch, messages);
    benchmark::DoNotOptimize(messages);
  }
}
BENCHMARK(BM_ParseBatchMessages);
