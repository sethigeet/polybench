#include <benchmark/benchmark.h>

#include "types/ring_buffer.hpp"

static void BM_RingBuffer_PushPop(benchmark::State& state) {
  RingBuffer<int, 1024> buffer;
  int value = 42;

  for (auto _ : state) {
    buffer.push(value);
    auto result = buffer.pop();
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_RingBuffer_PushPop);

static void BM_RingBuffer_Push(benchmark::State& state) {
  RingBuffer<int, 1024> buffer;
  int value = 42;

  for (auto _ : state) {
    state.PauseTiming();
    // Drain buffer to avoid filling up
    while (!buffer.empty()) {
      buffer.pop();
    }
    state.ResumeTiming();

    for (int i = 0; i < 100; ++i) {
      buffer.push(value);
    }
    benchmark::DoNotOptimize(buffer.size());
  }
}
BENCHMARK(BM_RingBuffer_Push);

static void BM_RingBuffer_Pop(benchmark::State& state) {
  RingBuffer<int, 1024> buffer;

  for (auto _ : state) {
    state.PauseTiming();
    // Fill buffer
    for (int i = 0; i < 100; ++i) {
      buffer.push(i);
    }
    state.ResumeTiming();

    for (int i = 0; i < 100; ++i) {
      auto result = buffer.pop();
      benchmark::DoNotOptimize(result);
    }
  }
}
BENCHMARK(BM_RingBuffer_Pop);
