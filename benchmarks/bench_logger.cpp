#include <benchmark/benchmark.h>

#include "logger.hpp"

// Measures the cost of a LOG_INFO call (message actually emitted)
static void BM_LogInfo(benchmark::State& state) {
  logger::init();
  for (auto _ : state) {
    logger::get("Engine").info("Perf frames={} bytes={} parsed={} avg_ns={:.1f}", 1000ULL, 50000ULL,
                               500ULL, 123.4);
  }
}
BENCHMARK(BM_LogInfo);

// Measures the cost of a LOG_DEBUG call filtered at INFO level (no message emitted)
static void BM_LogDebug_Filtered(benchmark::State& state) {
  logger::init();
  for (auto _ : state) {
    logger::get("Engine").debug("Book snapshot for asset {} in market {}: {} bids, {} asks",
                                "test-asset", "test-market", 50, 50);
  }
}
BENCHMARK(BM_LogDebug_Filtered);
