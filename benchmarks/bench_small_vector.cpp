#include <benchmark/benchmark.h>

#include "types/small_vector.hpp"

static void BM_SmallVector_PushBack_Inline(benchmark::State& state) {
  for (auto _ : state) {
    SmallVector<int, 16> vec;
    for (int i = 0; i < 16; ++i) {
      vec.push_back(i);
    }
    benchmark::DoNotOptimize(vec.data());
  }
}
BENCHMARK(BM_SmallVector_PushBack_Inline);

static void BM_SmallVector_PushBack_Heap(benchmark::State& state) {
  for (auto _ : state) {
    SmallVector<int, 16> vec;
    for (int i = 0; i < 64; ++i) {
      vec.push_back(i);
    }
    benchmark::DoNotOptimize(vec.data());
  }
}
BENCHMARK(BM_SmallVector_PushBack_Heap);

static void BM_SmallVector_Iterate(benchmark::State& state) {
  SmallVector<int, 64> vec;
  for (int i = 0; i < 64; ++i) {
    vec.push_back(i);
  }

  for (auto _ : state) {
    int sum = 0;
    for (const auto& v : vec) {
      sum += v;
    }
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_SmallVector_Iterate);

static void BM_SmallVector_Reserve(benchmark::State& state) {
  for (auto _ : state) {
    SmallVector<int, 16> vec;
    vec.reserve(1024);
    for (int i = 0; i < 1024; ++i) {
      vec.push_back(i);
    }
    benchmark::DoNotOptimize(vec.data());
  }
}
BENCHMARK(BM_SmallVector_Reserve);
