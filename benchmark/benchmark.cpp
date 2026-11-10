// Throughput benchmarks (Google Benchmark). For latency percentiles see latency.cpp.
#include "common.hpp"
#include "mpmc_ring_buffer.hpp"
#include "spsc_ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

using Spsc = SpscRingBuffer<std::uint64_t, 4096>;
using Mpmc = MpmcRingBuffer<std::uint64_t, 4096>;
using Mutex = MutexQueue<std::uint64_t, 4096>;

// One thread, push then pop: raw per-operation cost with no cross-core traffic.
template <class Q> static void BM_Uncontended(benchmark::State &state) {
  auto q = std::make_unique<Q>();
  for (auto _ : state) {
    q->push(42);
    auto v = q->pop();
    benchmark::DoNotOptimize(v);
  }
  state.SetItemsProcessed(state.iterations());
}

// One producer thread and one consumer (the benchmark thread), pinned to different
// cores. Reports items/s through the queue.
template <class Q> static void BM_ProducerConsumer(benchmark::State &state) {
  auto q = std::make_unique<Q>();
  std::atomic<bool> done{false};

  std::thread producer([&] {
    pin_thread(cpu_from_env("LFRB_CPU_PRODUCER", 2));
    std::uint64_t i = 0;
    while (!done.load(std::memory_order_relaxed)) {
      if (q->push(i)) ++i;
    }
  });
  pin_thread(cpu_from_env("LFRB_CPU_CONSUMER", 4));

  for (auto _ : state) {
    std::optional<std::uint64_t> v;
    while (!(v = q->pop())) {
    }
    benchmark::DoNotOptimize(v);
  }

  done.store(true, std::memory_order_relaxed);
  producer.join();
  state.SetItemsProcessed(state.iterations());
}

// N threads share one queue; each does push then pop. Shows how each design
// degrades as contention grows (SPSC is excluded: it is not safe here).
template <class Q> static void BM_Contended(benchmark::State &state) {
  static auto q = std::make_unique<Q>(); // shared by all benchmark threads
  for (auto _ : state) {
    while (!q->push(1)) {
    }
    std::optional<std::uint64_t> v;
    while (!(v = q->pop())) {
    }
    benchmark::DoNotOptimize(v);
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(BM_Uncontended, Spsc);
BENCHMARK_TEMPLATE(BM_Uncontended, Mpmc);
BENCHMARK_TEMPLATE(BM_Uncontended, Mutex);

BENCHMARK_TEMPLATE(BM_ProducerConsumer, Spsc)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ProducerConsumer, Mpmc)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ProducerConsumer, Mutex)->UseRealTime();

BENCHMARK_TEMPLATE(BM_Contended, Mpmc)->ThreadRange(1, 8)->UseRealTime();
BENCHMARK_TEMPLATE(BM_Contended, Mutex)->ThreadRange(1, 8)->UseRealTime();

BENCHMARK_MAIN();
