#include "mpmc_ring_buffer.hpp"
#include "spsc_ring_buffer.hpp"
#include <benchmark/benchmark.h>
#include <mutex>
#include <queue>
#include <thread>

#if defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
// Affinity tag hint — not a strict pin on macOS, but nudges the scheduler
// to place threads with different tags on different physical cores.
static void pin_thread_to_core(int tag) {
    thread_affinity_policy_data_t policy = {tag};
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy),
                      THREAD_AFFINITY_POLICY_COUNT);
}
#elif defined(__linux__)
#include <pthread.h>
static void pin_thread_to_core(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}
#else
static void pin_thread_to_core(int) {}
#endif

// Baseline: mutex-protected std::queue, single-threaded (no contention)
static void BM_MutexQueue(benchmark::State &state) {
  std::queue<int> q;
  std::mutex m;
  for (auto _ : state) {
    std::lock_guard<std::mutex> lk(m);
    q.push(42);
    auto val = q.front();
    q.pop();
    benchmark::DoNotOptimize(val);
  }
  state.SetItemsProcessed(state.iterations());
}

// Mutex-protected std::queue, two threads (matches BM_SPSCThroughput setup)
// Uses spinning (no condvar) so the comparison is apples-to-apples.
static void BM_MutexQueueThroughput(benchmark::State &state) {
  std::queue<int> q;
  std::mutex m;
  std::atomic<bool> done{false};

  std::thread producer([&] {
    while (!done.load(std::memory_order_relaxed)) {
      std::lock_guard<std::mutex> lk(m);
      q.push(42);
    }
  });

  for (auto _ : state) {
    while (true) {
      std::lock_guard<std::mutex> lk(m);
      if (!q.empty()) {
        auto val = q.front();
        q.pop();
        benchmark::DoNotOptimize(val);
        break;
      }
    }
  }

  done.store(true);
  producer.join();
  state.SetItemsProcessed(state.iterations());
}

// Lock-free SPSC
static void BM_SPSCPush(benchmark::State &state) {
  SpscRingBuffer<int, 4096> rb;
  for (auto _ : state) {
    rb.push(42);
    auto val = rb.pop();
    benchmark::DoNotOptimize(val);
  }
  state.SetItemsProcessed(state.iterations());
}

// Two-thread SPSC throughput
static void BM_SPSCThroughput(benchmark::State &state) {
  SpscRingBuffer<int, 4096> rb;
  std::atomic<bool> done{false};

  std::thread producer([&] {
    while (!done.load(std::memory_order_relaxed))
      rb.push(42);
  });

  for (auto _ : state) {
    while (!rb.pop()) {
    }
  }

  done.store(true);
  producer.join();
  state.SetItemsProcessed(state.iterations());
}

static void BM_SPSCOneWayLatency(benchmark::State &state) {
  SpscRingBuffer<uint64_t, 4096> rb;
  std::atomic<bool> done{false};
  std::atomic<uint64_t> result{0};

  // Producer: stamps a value and pushes it
  std::thread producer([&] {
    pin_thread_to_core(2);
    while (!done.load(std::memory_order_relaxed)) {
      uint64_t ts = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
      // Check done inside the retry loop so a full buffer doesn't deadlock
      // when the consumer stops consuming after its benchmark loop ends.
      while (!rb.push(ts)) {
        if (done.load(std::memory_order_relaxed)) { return; }
      }
    }
  });

  pin_thread_to_core(3);

  for (auto _ : state) {
    std::optional<uint64_t> val;
    while (!(val = rb.pop()).has_value()) {}
    uint64_t sent = *val;
    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    result.store(now - sent, std::memory_order_relaxed);
    benchmark::DoNotOptimize(result);
  }

  done.store(true);
  producer.join();
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_MutexQueue);
BENCHMARK(BM_MutexQueueThroughput);
BENCHMARK(BM_SPSCPush);
BENCHMARK(BM_SPSCThroughput)->Threads(1);
BENCHMARK(BM_SPSCOneWayLatency);

BENCHMARK_MAIN();
