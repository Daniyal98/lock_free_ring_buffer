# Lock-Free Ring Buffers

[![CI](https://github.com/Daniyal98/lock_free_ring_buffer/actions/workflows/ci.yml/badge.svg)](https://github.com/Daniyal98/lock_free_ring_buffer/actions/workflows/ci.yml)

Header-only C++20 bounded queues for passing messages between threads:

- **`SpscRingBuffer<T, N>`**: single producer, single consumer. Index-based, with cached indices.
- **`MpmcRingBuffer<T, N>`**: multiple producers, multiple consumers. Dmitry Vyukov's sequence-slot design.

Both take a power-of-two capacity, allocate nothing after construction, return `false` from `push()` when full and `std::nullopt` from `pop()` when empty. Payloads don't have to be trivially copyable (e.g. `std::string` works).

```cpp
#include "spsc_ring_buffer.hpp"

auto q = std::make_unique<SpscRingBuffer<Order, 1024>>();
q->push(order);                    // producer thread; false if full
if (auto o = q->pop()) handle(*o); // consumer thread; nullopt if empty
```

## Design

### SPSC

| Step | Ordering | Why |
| --- | --- | --- |
| Producer reads its own `tail_` | relaxed | Only the producer writes it |
| Producer re-reads `head_` (only when its cache says full) | acquire | Pairs with the consumer's release: the slot has been read and can be overwritten |
| Producer writes the slot, then stores `tail_ + 1` | release | The slot write becomes visible before the new index |
| Consumer re-reads `tail_` (only when its cache says empty) | acquire | Pairs with the producer's release: the slot contents are visible |
| Consumer moves the slot out, then stores `head_ + 1` | release | The read finishes before the producer can reuse the slot |

- **Cached indices.** Each side keeps a private copy of the other side's index and reloads the shared one only when the copy says full or empty. In the common case neither thread touches a cache line the other one writes.
- **Layout.** `tail_` and the producer's cached `head_` share one 64-byte line; `head_` and the consumer's cached `tail_` share another; the buffer starts on a third.
- **Slots are plain `T`.** The release/acquire on the index already orders the slot data, so per-slot atomics would add nothing. Per-slot atomics would also stop compiling for non-trivial `T`, and would silently fall back to a lock for large `T`.

### MPMC

Each slot holds a sequence number. `sequence == pos` means the slot is free for the producer that claims position `pos`, and `sequence == pos + 1` means it holds data for the consumer that claims `pos`. Producers claim positions with a CAS on `tail_`, write the slot, then publish it with a release store of the sequence. Consumers do the same on `head_`. Each slot takes a full cache line, so neighbouring threads don't false-share.

**Progress caveat:** there's no mutex, but this queue isn't lock-free in the formal sense. If a producer is preempted after winning the CAS and before publishing its slot, consumers that reach that slot see "empty" until the producer resumes.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sanitizer builds:

```bash
cmake -B build-tsan -DLFRB_SANITIZER=thread  -DLFRB_BUILD_BENCHMARKS=OFF   # ThreadSanitizer
cmake -B build-asan -DLFRB_SANITIZER=address -DLFRB_BUILD_BENCHMARKS=OFF   # AddressSanitizer + UBSan
```

The tests cover:

- Empty and full queues, including that a queue holds exactly its capacity.
- FIFO order across thousands of wraparounds.
- `std::string` payloads.
- A two-thread SPSC stress test that moves 1M items and checks every value arrives in order.
- A 4-producer / 4-consumer MPMC stress test that checks exactly-once delivery and that each consumer sees each producer's items in push order.

[CI](.github/workflows/ci.yml) runs GCC and Clang, each in Release, TSan and ASan+UBSan, on every push.

The stress tests catch ordering bugs: weakening the SPSC `tail_` release/acquire pair to `relaxed` makes ThreadSanitizer report a data race in `pop()`.

## Benchmarks

```bash
benchmark/run_bench.sh build 2 4   # build dir, then two CPUs on different physical cores
```

- **Latency** ([`latency.cpp`](benchmark/latency.cpp)): two threads pinned to different cores play ping-pong through a pair of queues. Only one message is in flight, so there's no queueing delay. It records 2M round trips after a 10% warm-up and reports the full percentile distribution. It uses `steady_clock`, so each sample includes about 20 ns of clock-read overhead. One-way latency is about RTT / 2.
- **Throughput** ([`benchmark.cpp`](benchmark/benchmark.cpp), Google Benchmark, median of 5 repetitions):
  - uncontended push+pop on one thread;
  - producer → consumer on two pinned cores;
  - N threads doing push+pop on one shared queue.
- **Baseline:** `MutexQueue`, the same bounded ring and interface guarded by `std::mutex`, so the comparison isolates the cost of synchronisation.
- **Core choice matters.** On the test machine, CPUs 2 and 3 are SMT siblings of one physical core and share L1/L2; CPUs 2 and 4 are separate cores. Check `lscpu -e` and pass two different `CORE`s.

**Test machine:** AMD Ryzen 7 260 laptop (8 cores / 16 threads), Linux 7.3 (CachyOS), GCC 16.2.1, `-O3 -march=native`. The `powersave` governor was active and no cores were isolated, so the `max` column shows OS interrupts and preemption, not the queues. These are single-machine numbers; rerun them on your own hardware.

### Round-trip latency, ns (CPU 2 ↔ CPU 4)

| Queue | p50 | p90 | p99 | p99.9 | p99.99 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SPSC  | 141 | 161 | 180 | 551 | 2,194 | 767,270 |
| MPMC  | 130 | 131 | 171 | 561 | 2,004 | 747,223 |
| Mutex | 871 | 1,413 | 2,204 | 4,929 | 10,710 | 672,203 |

### SPSC before and after this revision (same tool, same machine, 2 runs each)

| Version | p50 | p99 | p99.9 |
| --- | ---: | ---: | ---: |
| Original (`835b776`: per-slot `std::atomic<T>`, no cached indices) | 170 | 201–260 | 591–701 |
| Current | 140–141 | 171–180 | 410–551 |

### Throughput, median

| Benchmark | SPSC | MPMC | Mutex |
| --- | ---: | ---: | ---: |
| Uncontended push+pop, 1 thread | 5.3 ns (191 M/s) | 7.4 ns (136 M/s) | 7.9 ns (128 M/s) |
| Producer → consumer, 2 cores | 9.5 M items/s | 10.6 M items/s | 8.9 M items/s |

| Threads sharing one queue | MPMC | Mutex |
| ---: | ---: | ---: |
| 1 | 89 M items/s | 93 M items/s |
| 2 | 40 M items/s | 47 M items/s |
| 4 | 18 M items/s | 24 M items/s |
| 8 | 8.7 M items/s | 13 M items/s |

SPSC producer → consumer throughput varied between 9.5 and 12.2 M items/s across runs on this machine.

### What the numbers say

1. **For hand-off latency, lock-free clearly wins.** Median round trip is about 130–140 ns against about 870 ns for the mutex queue, and roughly 12× lower at p99. A one-way SPSC hand-off of about 70 ns is close to the cost of one cross-core cache-line transfer on this CPU.
2. **MPMC slightly beats SPSC on latency and saturated throughput.** A likely cause: MPMC keeps the sequence number and the data in the same cache line, so a hand-off moves one line, while the index-based SPSC moves two (`tail_` and the slot). When the queue runs full, adjacent 8-byte SPSC slots also share lines between the producer's write and the consumer's read. Next steps: confirm with `perf c2c`, try a per-slot flag or batched index publication for the SPSC, and re-measure.
3. **Under heavy contention, the mutex queue beats the CAS-based MPMC** (8 threads: 13 M vs 8.7 M items/s). Here lock-free buys freedom from blocking, not throughput: threads never sleep on a futex or wait on a preempted lock holder. A likely cost is CAS-retry traffic on the shared `head_`/`tail_` lines.
