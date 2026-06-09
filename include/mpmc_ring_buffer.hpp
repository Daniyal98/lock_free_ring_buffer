#pragma once
#include <array>
#include <atomic>
#include <optional>

template <typename T, std::size_t Capacity> class MpmcRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  static constexpr std::size_t Mask = Capacity - 1;

  struct Slot {
    alignas(64) std::atomic<std::size_t> sequence{0};
    T data;
  };

  alignas(64) std::array<Slot, Capacity> slots;
  alignas(64) std::atomic<std::size_t> head{0};
  alignas(64) std::atomic<std::size_t> tail{0};

public:
  MpmcRingBuffer() {
    for (std::size_t i = 0; i < Capacity; ++i) {
      slots[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  bool push(const T &item) noexcept {
    std::size_t pos = tail.load(std::memory_order_relaxed);

    while (true) {
      Slot &slot = slots[pos & Mask];
      std::size_t seq = slot.sequence.load(std::memory_order_acquire);
      std::intptr_t diff =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

      if (diff == 0) {
        if (tail.compare_exchange_weak(pos, pos + 1,
                                       std::memory_order_relaxed)) {
          slot.data = item;
          slot.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        return false; // buffer full
      } else {
        pos = tail.load(std::memory_order_relaxed); // retry
      }
    }
  }

  std::optional<T> pop() noexcept {
    std::size_t pos = head.load(std::memory_order_relaxed);

    while (true) {
      Slot &slot = slots[pos & Mask];
      std::size_t seq = slot.sequence.load(std::memory_order_acquire);
      std::intptr_t diff = static_cast<std::intptr_t>(seq) -
                           static_cast<std::intptr_t>(pos + 1);

      if (diff == 0) {
        if (head.compare_exchange_weak(pos, pos + 1,
                                       std::memory_order_relaxed)) {
          T item = slot.data;
          slot.sequence.store(pos + Capacity, std::memory_order_release);
          return item;
        }
      } else if (diff < 0) {
        return std::nullopt; // buffer empty
      } else {
        pos = head.load(std::memory_order_relaxed);
      }
    }
  }
};
