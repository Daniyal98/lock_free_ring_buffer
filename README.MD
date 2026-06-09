# Lock-Free Ring Buffer

Header-only C++20 lock-free ring buffers for high-performance inter-thread communication. Two implementations are provided:

- **`SpscRingBuffer<T, N>`** — Single-Producer Single-Consumer. Uses two cache-line-separated atomics (`head`, `tail`) with acquire/release ordering. Zero locks, zero heap allocation after construction.
- **`MpmcRingBuffer<T, N>`** — Multi-Producer Multi-Consumer. Uses a slot-sequence array with `compare_exchange_weak` to arbitrate concurrent producers and consumers without a mutex.

Both require a power-of-two capacity and are fixed-size (allocated on the stack or as a member).

## Usage

```cpp
#include "spsc_ring_buffer.hpp"

SpscRingBuffer<int, 1024> rb;

// Producer thread
rb.push(42);          // returns false if full

// Consumer thread
auto val = rb.pop();  // returns std::optional<T>, nullopt if empty
```

```cpp
#include "mpmc_ring_buffer.hpp"

MpmcRingBuffer<int, 1024> rb;

// Any thread
rb.push(42);
auto val = rb.pop();
```

## Building

Requires CMake 3.20+ and a C++20 compiler. Google Benchmark is fetched automatically if not installed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Optional flags:

| Flag | Default | Description |
|---|---|---|
| `LFRB_BUILD_TESTS` | `ON` | Build GTest unit tests |
| `LFRB_BUILD_BENCHMARKS` | `ON` | Build Google Benchmark suite |

## Running Benchmarks

```bash
cd build
../benchmark/run_bench.sh ./ring_buffer_bench results.json
```

The script pins threads to specific cores on Linux (`taskset`) and uses `caffeinate` on macOS to prevent CPU throttling during the run.

## Benchmark Results

Measured on Apple M-series (8 cores, 64 KB L1-D, 4 MB L2), macOS 15.5, AppleClang 17, `-O3 -march=native`. 10 repetitions, aggregates only.

| Benchmark | Threads | Mean latency | Throughput | CV |
|---|:---:|---:|---:|---:|
| `MutexQueue` (single-thread) | 1 | 9.4 ns | 108M ops/s | 2.5% |
| `SPSCPush` (single-thread) | 1 | 10.8 ns | 93M ops/s | 0.4% |
| `MutexQueueThroughput` (2-thread) | 2 | 58.0 ns | ~17M ops/s | 8.5% |
| `SPSCThroughput` (2-thread) | 2 | 53.4 ns | ~19M ops/s | 10.5% |
| `SPSCOneWayLatency` (2-thread) | 2 | 71.3 ns | — | 3.5% |

### What the numbers mean

**Single-threaded (no contention):** The uncontended mutex is marginally faster (9.4 ns vs 10.8 ns) because `os_unfair_lock` on Apple Silicon reduces to a single CAS with no waiters, while the SPSC still pays for acquire/release atomics on every operation. Neither wins by a meaningful margin here.

**Two-thread throughput:** This is where lock-free design matters. Both benchmarks have a dedicated producer thread pushing as fast as possible while the benchmark thread consumes. End-to-end wall-clock latency: SPSC at **53.4 ns** vs mutex at **58.0 ns** — an 8% improvement. More significantly, the mutex consumer burns extra CPU cycles spinning to acquire a lock the producer is holding; the SPSC consumer reads a single atomic with no contention.

**One-way latency (71.3 ns):** Measures the time from a timestamp being written by the producer to it being read by the consumer — pure cross-core communication cost. Both threads use `CLOCK_UPTIME_RAW` (Apple Silicon hardware timebase) for nanosecond-accurate stamping. The ~18 ns gap above throughput latency reflects the timestamp overhead added per item.

**Key takeaway:** Lock-free SPSC wins under real producer/consumer load, and the advantage grows with contention. On a system where the mutex would be contested by multiple producers or consumers, `MpmcRingBuffer` eliminates that entirely at the cost of a slightly higher per-operation CAS.
