// Round-trip latency with percentiles.
//
// Two threads on different cores play ping-pong through a pair of queues: the main
// thread timestamps, pushes into `ping`, and spins until the echo thread's reply
// arrives on `pong`. Only one message is in flight, so the result is pure
// cross-core hand-off cost with no queueing delay. One-way latency ~= RTT / 2.
//
// Usage: ring_buffer_latency [cpu_a] [cpu_b] [iterations]
#include "common.hpp"
#include "mpmc_ring_buffer.hpp"
#include "spsc_ring_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static std::int64_t ns_between(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

static std::int64_t percentile(const std::vector<std::int64_t> &sorted, double p) {
  const auto i = static_cast<std::size_t>(p * static_cast<double>(sorted.size()));
  return sorted[std::min(i, sorted.size() - 1)];
}

template <class Q>
static void run(const char *name, int cpu_a, int cpu_b, std::size_t iters) {
  const std::size_t warmup = iters / 10;
  const std::size_t total = iters + warmup;
  auto ping = std::make_unique<Q>();
  auto pong = std::make_unique<Q>();

  std::thread echo([&] {
    pin_thread(cpu_b);
    for (std::size_t i = 0; i < total; ++i) {
      std::optional<std::uint64_t> v;
      while (!(v = ping->pop())) {
      }
      while (!pong->push(*v)) {
      }
    }
  });

  pin_thread(cpu_a);
  std::vector<std::int64_t> rtt;
  rtt.reserve(iters);
  for (std::size_t i = 0; i < total; ++i) {
    const auto t0 = Clock::now();
    while (!ping->push(i)) {
    }
    std::optional<std::uint64_t> v;
    while (!(v = pong->pop())) {
    }
    const auto t1 = Clock::now();
    if (i >= warmup) rtt.push_back(ns_between(t0, t1));
  }
  echo.join();

  std::sort(rtt.begin(), rtt.end());
  std::printf("| %-5s | %6lld | %6lld | %6lld | %7lld | %8lld | %8lld |\n", name,
              (long long)percentile(rtt, 0.50), (long long)percentile(rtt, 0.90),
              (long long)percentile(rtt, 0.99), (long long)percentile(rtt, 0.999),
              (long long)percentile(rtt, 0.9999), (long long)rtt.back());
}

int main(int argc, char **argv) {
  const int cpu_a = argc > 1 ? std::atoi(argv[1]) : 2;
  const int cpu_b = argc > 2 ? std::atoi(argv[2]) : 4;
  const std::size_t iters = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 2'000'000;

  // Cost of the two clock reads that bracket every sample (median of back-to-back reads).
  pin_thread(cpu_a);
  std::vector<std::int64_t> overhead(100'000);
  for (auto &o : overhead) {
    const auto a = Clock::now();
    o = ns_between(a, Clock::now());
  }
  std::sort(overhead.begin(), overhead.end());

  std::printf("Round-trip latency, ns (CPUs %d <-> %d, %zu samples, clock overhead ~%lld ns)\n\n",
              cpu_a, cpu_b, iters, (long long)percentile(overhead, 0.5));
  std::printf("| Queue |    p50 |    p90 |    p99 |   p99.9 |  p99.99 |      max |\n");
  std::printf("| ----- | -----: | -----: | -----: | ------: | -------: | -------: |\n");
  run<SpscRingBuffer<std::uint64_t, 1024>>("SPSC", cpu_a, cpu_b, iters);
  run<MpmcRingBuffer<std::uint64_t, 1024>>("MPMC", cpu_a, cpu_b, iters);
  run<MutexQueue<std::uint64_t, 1024>>("Mutex", cpu_a, cpu_b, iters);
}
