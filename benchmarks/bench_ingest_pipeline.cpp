#include <benchmark/benchmark.h>

#include "ingest_pipeline.hpp"

static const std::string kTransportBookMessageJson = R"({
  "event_type": "book",
  "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836455",
  "market": "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
  "timestamp": "1704067200000000",
  "bids": [
    {"price": "0.53", "size": "100.5"},
    {"price": "0.52", "size": "200.0"}
  ],
  "asks": [
    {"price": "0.55", "size": "120.0"},
    {"price": "0.56", "size": "180.0"}
  ]
})";

static void BM_IngestPipeline_FrameToQueue(benchmark::State& state) {
  PerfStats perf_stats({.enabled = false});
  MessagePipeline pipeline(1024, &perf_stats);
  SmallVector<PolymarketMessage, 16> messages;

  for (auto _ : state) {
    benchmark::DoNotOptimize(pipeline.ingest_message(kTransportBookMessageJson));
    messages.clear();
    benchmark::DoNotOptimize(pipeline.poll_messages(messages, 16));
  }
}
BENCHMARK(BM_IngestPipeline_FrameToQueue);

// Stats ENABLED — measures overhead of recording + timing on the hot path
static void BM_IngestPipeline_FrameToQueue_WithStats(benchmark::State& state) {
  PerfStats perf_stats({.enabled = true, .log_interval_messages = 1000000});
  MessagePipeline pipeline(1024, &perf_stats);
  SmallVector<PolymarketMessage, 16> messages;

  for (auto _ : state) {
    benchmark::DoNotOptimize(pipeline.ingest_message(kTransportBookMessageJson));
    messages.clear();
    benchmark::DoNotOptimize(pipeline.poll_messages(messages, 16));
  }
}
BENCHMARK(BM_IngestPipeline_FrameToQueue_WithStats);
