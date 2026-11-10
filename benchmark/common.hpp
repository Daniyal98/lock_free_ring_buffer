#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

// Baseline for comparison: same bounded ring, same interface, guarded by a mutex.
// Bounded and allocation-free so the comparison isolates the synchronisation cost.
template <typename T, std::size_t Capacity> class MutexQueue {
  std::mutex m_;
  std::array<T, Capacity> buf_{};
  std::size_t head_ = 0, tail_ = 0;

public:
  bool push(const T &item) {
    std::lock_guard lk(m_);
    if (tail_ - head_ == Capacity) return false;
    buf_[tail_++ % Capacity] = item;
    return true;
  }

  std::optional<T> pop() {
    std::lock_guard lk(m_);
    if (head_ == tail_) return std::nullopt;
    return std::optional<T>{std::move(buf_[head_++ % Capacity])};
  }
};

// Pin the calling thread to one logical CPU. No-op off Linux.
inline void pin_thread(int cpu) {
#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
  (void)cpu;
#endif
}

// CPUs come from the environment so they can be matched to the machine's topology.
// Pick two different *physical* cores (check `lscpu -e`): SMT siblings share L1/L2
// and make cross-thread numbers look far better than cross-core reality.
inline int cpu_from_env(const char *name, int fallback) {
  const char *v = std::getenv(name);
  return v ? std::atoi(v) : fallback;
}
