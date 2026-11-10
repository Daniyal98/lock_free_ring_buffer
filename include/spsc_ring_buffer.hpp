#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

// Bounded single-producer / single-consumer queue.
// Exactly one thread may call push() and exactly one other thread may call pop().
//
// Memory ordering: the producer writes the slot, then publishes it with a release
// store of tail_; the consumer's acquire load of tail_ makes the slot write visible.
// The same pairing on head_ tells the producer a slot is free to overwrite.
// Slots are plain T: the index release/acquire already orders the data, so no
// per-slot atomics are needed (and large T stays lock-free).
//
// Each side keeps a cached copy of the other side's index and only re-reads the
// shared one when the cache says full/empty, so the common case touches no
// cache line owned by the other core.
template <typename T, std::size_t Capacity> class SpscRingBuffer {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  static constexpr std::size_t Mask = Capacity - 1;

  // Producer-owned cache line: write index + producer's view of the read index.
  alignas(64) std::atomic<std::size_t> tail_{0};
  std::size_t head_cache_{0};

  // Consumer-owned cache line: read index + consumer's view of the write index.
  alignas(64) std::atomic<std::size_t> head_{0};
  std::size_t tail_cache_{0};

  alignas(64) std::array<T, Capacity> buffer_{};

public:
  // Returns false if the queue is full.
  bool push(const T &item) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - head_cache_ == Capacity) {
      head_cache_ = head_.load(std::memory_order_acquire);
      if (tail - head_cache_ == Capacity) {
        return false;
      }
    }
    buffer_[tail & Mask] = item;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Returns std::nullopt if the queue is empty.
  std::optional<T> pop() {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_cache_) {
      tail_cache_ = tail_.load(std::memory_order_acquire);
      if (head == tail_cache_) {
        return std::nullopt;
      }
    }
    std::optional<T> item{std::move(buffer_[head & Mask])};
    head_.store(head + 1, std::memory_order_release);
    return item;
  }

  // Snapshot only: may be stale by the time it returns if other threads are active.
  // head_ is loaded first so the result can never underflow.
  std::size_t size() const noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    return tail_.load(std::memory_order_acquire) - head;
  }

  bool empty() const noexcept { return size() == 0; }

  static constexpr std::size_t capacity() noexcept { return Capacity; }
};
