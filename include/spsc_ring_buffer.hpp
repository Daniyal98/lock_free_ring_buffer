#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    static constexpr std::size_t Mask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head{0};
    alignas(64) std::atomic<std::size_t> tail{0};

    alignas(64) std::array<std::atomic<T>, Capacity> buffer{};
};