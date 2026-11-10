#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

// Bounded multi-producer / multi-consumer queue (Dmitry Vyukov's sequence-slot design).
//
// Each slot carries a sequence number that says whose turn it is:
//   sequence == pos          -> slot is free for the producer that claims position pos
//   sequence == pos + 1      -> slot holds data for the consumer that claims position pos
// A producer claims a position with a CAS on tail_, writes the data, then publishes it
// with a release store of the slot's sequence. Consumers mirror this on head_.
//
// Progress caveat: there is no mutex, but this is not lock-free in the formal sense.
// A producer preempted between winning the CAS and publishing the slot makes
// consumers that reach that slot report "empty" until it resumes.
template <typename T, std::size_t Capacity> class MpmcRingBuffer {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  static constexpr std::size_t Mask = Capacity - 1;

  // One slot per cache line so neighbouring producers/consumers don't false-share.
  struct alignas(64) Slot {
    std::atomic<std::size_t> sequence;
    T data;
  };

  std::array<Slot, Capacity> slots_;
  alignas(64) std::atomic<std::size_t> tail_{0}; // next position to push
  alignas(64) std::atomic<std::size_t> head_{0}; // next position to pop

public:
  MpmcRingBuffer() {
    for (std::size_t i = 0; i < Capacity; ++i) {
      slots_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  // Returns false if the queue is full.
  bool push(const T &item) {
    std::size_t pos = tail_.load(std::memory_order_relaxed);
    for (;;) {
      Slot &slot = slots_[pos & Mask];
      const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
      // Unsigned subtraction then conversion: well defined and wrap-safe.
      const auto diff = static_cast<std::ptrdiff_t>(seq - pos);
      if (diff == 0) {
        if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          slot.data = item;
          slot.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
        // CAS failure reloaded pos; retry with the new value.
      } else if (diff < 0) {
        return false; // slot still holds data from one lap ago: full
      } else {
        pos = tail_.load(std::memory_order_relaxed); // another producer moved ahead
      }
    }
  }

  // Returns std::nullopt if the queue is empty (or the next slot is not yet published).
  std::optional<T> pop() {
    std::size_t pos = head_.load(std::memory_order_relaxed);
    for (;;) {
      Slot &slot = slots_[pos & Mask];
      const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
      const auto diff = static_cast<std::ptrdiff_t>(seq - (pos + 1));
      if (diff == 0) {
        if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          std::optional<T> item{std::move(slot.data)};
          slot.sequence.store(pos + Capacity, std::memory_order_release);
          return item;
        }
      } else if (diff < 0) {
        return std::nullopt;
      } else {
        pos = head_.load(std::memory_order_relaxed);
      }
    }
  }

  static constexpr std::size_t capacity() noexcept { return Capacity; }
};
