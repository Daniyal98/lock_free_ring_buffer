#include "mpmc_ring_buffer.hpp"
#include "spsc_ring_buffer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Queues are heap-allocated: an MPMC slot is a full cache line, so large
// capacities don't belong on the stack.

// ---------------------------------------------------------------- SPSC

TEST(Spsc, EmptyPopReturnsNullopt) {
  auto q = std::make_unique<SpscRingBuffer<int, 8>>();
  EXPECT_TRUE(q->empty());
  EXPECT_FALSE(q->pop().has_value());
}

TEST(Spsc, HoldsExactlyCapacityItems) {
  auto q = std::make_unique<SpscRingBuffer<int, 8>>();
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(q->push(i));
  }
  EXPECT_FALSE(q->push(99));
  EXPECT_EQ(q->size(), 8u);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(q->pop(), i);
  }
  EXPECT_FALSE(q->pop().has_value());
}

TEST(Spsc, FifoAcrossManyWraparounds) {
  auto q = std::make_unique<SpscRingBuffer<int, 4>>();
  int next_in = 0, next_out = 0;
  for (int round = 0; round < 1000; ++round) {
    while (q->push(next_in)) ++next_in;
    for (int k = 0; k < 3; ++k) EXPECT_EQ(q->pop(), next_out++);
  }
}

TEST(Spsc, NonTriviallyCopyableType) {
  auto q = std::make_unique<SpscRingBuffer<std::string, 4>>();
  ASSERT_TRUE(q->push(std::string(100, 'x'))); // longer than any SSO buffer
  EXPECT_EQ(q->pop(), std::string(100, 'x'));
}

TEST(Spsc, TwoThreadStressPreservesOrder) {
  constexpr std::uint64_t kItems = 1'000'000;
  auto q = std::make_unique<SpscRingBuffer<std::uint64_t, 1024>>();

  std::thread producer([&] {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!q->push(i)) {
      }
    }
  });

  for (std::uint64_t expected = 0; expected < kItems; ++expected) {
    std::optional<std::uint64_t> v;
    while (!(v = q->pop())) {
    }
    ASSERT_EQ(*v, expected);
  }
  producer.join();
  EXPECT_TRUE(q->empty());
}

// ---------------------------------------------------------------- MPMC

TEST(Mpmc, EmptyPopReturnsNullopt) {
  auto q = std::make_unique<MpmcRingBuffer<int, 8>>();
  EXPECT_FALSE(q->pop().has_value());
}

TEST(Mpmc, HoldsExactlyCapacityItemsInFifoOrder) {
  auto q = std::make_unique<MpmcRingBuffer<int, 8>>();
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(q->push(i));
  }
  EXPECT_FALSE(q->push(99));
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(q->pop(), i);
  }
  EXPECT_FALSE(q->pop().has_value());
}

TEST(Mpmc, FifoAcrossManyWraparounds) {
  auto q = std::make_unique<MpmcRingBuffer<int, 4>>();
  int next_in = 0, next_out = 0;
  for (int round = 0; round < 1000; ++round) {
    while (q->push(next_in)) ++next_in;
    for (int k = 0; k < 3; ++k) EXPECT_EQ(q->pop(), next_out++);
  }
}

TEST(Mpmc, NonTriviallyCopyableType) {
  auto q = std::make_unique<MpmcRingBuffer<std::string, 4>>();
  ASSERT_TRUE(q->push(std::string(100, 'y')));
  EXPECT_EQ(q->pop(), std::string(100, 'y'));
}

// N producers, N consumers. Every item must be delivered exactly once, and each
// consumer must see any single producer's items in the order they were pushed.
TEST(Mpmc, ManyProducersManyConsumersStress) {
  constexpr int kThreads = 4;
  constexpr std::uint64_t kPerProducer = 200'000;
  constexpr std::uint64_t kTotal = kThreads * kPerProducer;
  auto q = std::make_unique<MpmcRingBuffer<std::uint64_t, 256>>();

  std::atomic<std::uint64_t> consumed{0};
  std::vector<std::vector<std::uint64_t>> received(kThreads);
  std::vector<std::thread> threads;

  for (int p = 0; p < kThreads; ++p) {
    threads.emplace_back([&, p] {
      for (std::uint64_t i = 0; i < kPerProducer; ++i) {
        const std::uint64_t value = static_cast<std::uint64_t>(p) * kPerProducer + i;
        while (!q->push(value)) {
        }
      }
    });
  }
  for (int c = 0; c < kThreads; ++c) {
    threads.emplace_back([&, c] {
      auto &mine = received[c];
      while (consumed.load(std::memory_order_relaxed) < kTotal) {
        if (auto v = q->pop()) {
          mine.push_back(*v);
          consumed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &t : threads) t.join();

  // Per-producer order as seen by each consumer.
  for (const auto &mine : received) {
    std::vector<std::uint64_t> last(kThreads, 0);
    std::vector<bool> seen_any(kThreads, false);
    for (std::uint64_t v : mine) {
      const auto producer = v / kPerProducer;
      if (seen_any[producer]) {
        ASSERT_GT(v, last[producer]) << "producer order violated";
      }
      seen_any[producer] = true;
      last[producer] = v;
    }
  }

  // Exactly-once delivery.
  std::vector<std::uint64_t> all;
  all.reserve(kTotal);
  for (const auto &mine : received) all.insert(all.end(), mine.begin(), mine.end());
  ASSERT_EQ(all.size(), kTotal);
  std::sort(all.begin(), all.end());
  for (std::uint64_t i = 0; i < kTotal; ++i) {
    ASSERT_EQ(all[i], i);
  }
  EXPECT_FALSE(q->pop().has_value());
}
