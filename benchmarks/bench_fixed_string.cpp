#include <benchmark/benchmark.h>

#include "types/fixed_string.hpp"

static void BM_FixedString_Construct(benchmark::State& state) {
  const char* str = "21742633143463906290569050155826241533067272736897614950488156847949938836455";
  for (auto _ : state) {
    FixedString<77> fs(str);
    benchmark::DoNotOptimize(fs.c_str());
  }
}
BENCHMARK(BM_FixedString_Construct);

static void BM_FixedString_Compare_Equal(benchmark::State& state) {
  FixedString<77> a(
      "21742633143463906290569050155826241533067272736897614950488156847949938836455");
  FixedString<77> b(
      "21742633143463906290569050155826241533067272736897614950488156847949938836455");

  for (auto _ : state) {
    bool result = (a == b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_FixedString_Compare_Equal);

static void BM_FixedString_Compare_NotEqual(benchmark::State& state) {
  FixedString<77> a(
      "21742633143463906290569050155826241533067272736897614950488156847949938836455");
  FixedString<77> b(
      "21742633143463906290569050155826241533067272736897614950488156847949938836456");

  for (auto _ : state) {
    bool result = (a == b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_FixedString_Compare_NotEqual);

static void BM_FixedString_Hash(benchmark::State& state) {
  FixedString<77> fs(
      "21742633143463906290569050155826241533067272736897614950488156847949938836455");
  std::hash<FixedString<77>> hasher;

  for (auto _ : state) {
    auto hash = hasher(fs);
    benchmark::DoNotOptimize(hash);
  }
}
BENCHMARK(BM_FixedString_Hash);
