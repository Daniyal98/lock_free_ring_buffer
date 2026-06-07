#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

template <typename T, std::size_t Capacity> class SpscRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  static constexpr std::size_t Mask = Capacity - 1;

  alignas(64) std::atomic<std::size_t> head{0};
  alignas(64) std::atomic<std::size_t> tail{0};

  alignas(64) std::array<std::atomic<T>, Capacity> buffer{};

public:
  bool push(const T &item) noexcept {
    std::size_t current_tail = tail.load(std::memory_order_relaxed);
    std::size_t next_tail = tail + 1;

    if (next_tail - head.load(std::memory_order_acquire) > Mask) {
      return false; // Buffer is full
    }

    buffer[current_tail & Mask].store(item, std::memory_order_release);
    tail.store(next_tail, std::memory_order_release);
    return true;
  }

  std::optional<T> pop() noexcept {
    std::size_t current_head = head.load(std::memory_order_relaxed);

    if (current_head == tail.load(std::memory_order_acquire)) {
      return std::nullopt; // Buffer is empty
    }

    T item = buffer[current_head & Mask].load(std::memory_order_acquire);
    head.store(current_head + 1, std::memory_order_release);
    return item;
  }

  bool empty() const noexcept {
    return head.load(std::memory_order_acquire) ==
           tail.load(std::memory_order_acquire);
  }

  std::size_t size() const noexcept {
    return tail.load(std::memory_order_acquire) -
           head.load(std::memory_order_acquire);
  }
};
