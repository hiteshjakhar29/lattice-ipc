[![CI](https://github.com/hiteshjakhar29/lattice-ipc/actions/workflows/ci.yml/badge.svg)](https://github.com/hiteshjakhar29/lattice-ipc/actions/workflows/ci.yml)

# lattice-ipc

C++20 lock-free inter-process shared-memory channel, microstructure signal engine, and real-time market-manipulation detector. It picks up where [ultrafast-feed](https://github.com/hiteshjakhar29/ultrafast-feed) leaves off: `FeedEvent`s captured via AF_XDP kernel bypass are moved across OS process boundaries through a POSIX shared-memory SPSC ring in sub-25 ns, decoded into a live order book, and fed simultaneously to a signal engine computing OFI, VAMP, OBI, microprice, and TFI, and to an anomaly detector running online Welford Z-scores for spoofing, layering, cancel-spike, and burst detection. Every hot-path component — IPC, signal computation, anomaly classification — is zero heap allocation, zero mutex, and measurably sub-microsecond.

---

## Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  NIC                                                                        │
│   │  raw UDP/multicast                                                      │
│   ▼                                                                         │
│  AF_XDP socket  (kernel bypass — ultrafast-feed)                            │
│   │  zero-copy DMA                                                          │
│   ▼                                                                         │
│  FeedHandler::rx_loop()   →   SpscRingBuffer<FeedEvent>  (intra-process)   │
│   │                                                                         │
│   │  FeedEvent { inject_ns, receive_ns, src_ip, src_port, payload[64] }    │
│   ▼                                                                         │
│  ShmWriter<FeedEvent, 65536>                                                │
│   │  atomic release store — write_idx                                       │
│   │  /dev/shm/lattice_feed  (mmap'd POSIX shm, one cache-line per index)   │
└───║─────────────────────────────────────────────────────────────────────────┘
    ║  OS process boundary
┌───║─────────────────────────────────────────────────────────────────────────┐
│   ║  atomic acquire load — write_idx                                        │
│  ShmReader<FeedEvent, 65536>          lattice-ipc (this repo)               │
│   │                                                                         │
│   ├──────────────────────┐                                                  │
│   ▼                      ▼                                                  │
│  SignalEngine        AnomalyDetector                                        │
│   │                      │                                                  │
│  OrderBook           WelfordStats (per order_id)                            │
│  (BBO cache)         BurstDetector                                          │
│   │                  LayeringDetector                                       │
│   │                  CancelSpikeDetector                                    │
│   │                  SymbolScorer                                           │
│   │                      │                                                  │
│   ▼                      ▼                                                  │
│  SignalSnapshot      SpoofAlert / BurstAlert / LayeringAlert                │
│  { mid, microprice,  { order_id, z_score, placed_ns, cancelled_ns }        │
│    ofi, vamp, obi,                                                          │
│    tfi, spread }         │                                                  │
│   │                      │                                                  │
│   └──────────┬───────────┘                                                  │
│              ▼                                                              │
│   downstream risk engine / execution strategy / surveillance                │
└─────────────────────────────────────────────────────────────────────────────┘
```

See [ultrafast-feed](https://github.com/hiteshjakhar29/ultrafast-feed) for the kernel-bypass packet capture layer.

---

## Architecture

### 1 — Lock-Free Shared Memory IPC (`lattice_shm`)

POSIX shared memory (`shm_open` + `mmap`) with a purpose-built `ShmLayout<T,N>` header is materially faster than any alternative for single-producer/single-consumer inter-process communication in HFT:

- **TCP/Unix sockets** add kernel scheduler round-trips and copy overhead. Even with `SO_BUSY_POLL`, you pay at minimum one context switch per message. POSIX shm with spinning costs ~21 ns end-to-end at 10 M events/s — no syscall on the hot path after `mmap`.
- **`std::atomic<T>` in mmap** is undefined behavior. The standard requires atomic objects to be constructed in place; mmap regions are not C++ objects. **`std::atomic_ref<uint64_t>`** (C++20) is the correct primitive: it applies atomic semantics to plain storage already live in the mapped region.
- **`acquire` / `release` ordering** — not `seq_cst` — is the right fence for SPSC. The producer needs only to ensure the slot write is visible before the index update (release store on `write_idx`). The consumer needs only to see all slot writes that happened before the matching index update (acquire load on `write_idx`). `seq_cst` would impose a full memory barrier on every operation — measurably slower with no correctness benefit in a two-party channel.

Layout:

```
ShmLayout<T,N>
├── hdr          [64 bytes]   magic sentinel, version, capacity, element_size
├── write_idx    [64 bytes]   producer index — plain uint64_t, accessed via atomic_ref
├── read_idx     [64 bytes]   consumer index — plain uint64_t, accessed via atomic_ref
└── slots[N]     [N × sizeof(T)]
```

Indices are monotonically increasing — no modulo on the counter, slot is `idx & (N−1)`. All N slots are usable (fullness = `write_idx − read_idx ≥ N`). Writer owns `shm_unlink` on destruction. On writer restart, `memset` zeroes the region and the magic sentinel is published last via a release store; the reader detects the index regression and calls `reattach()` to skip stale events.

Every error path returns a typed `ShmError` (9 codes: `SegmentNotFound`, `PermissionDenied`, `BadMagic`, `VersionMismatch`, `SizeMismatch`, …). `is_healthy()` on both writer and reader performs one `fstat(2)` and a header re-validation — safe to call from a monitor thread, never on the write/read hot path.

---

### 2 — Microstructure Signal Engine (`lattice_signals`)

Processes every `FeedEvent` through a live order book and produces a `SignalSnapshot` on each best-bid/offer change. Five signals computed per BBO update, all in sub-200 ns.

**Order Flow Imbalance (OFI)**
```
ofi_delta = Δbid_qty_at_best − Δask_qty_at_best
ofi       = rolling mean of ofi_delta over last 10 BBO events
```
Measures net order flow pressure at the top of book. Consistently positive OFI precedes short-term price increases; negative OFI precedes decreases. Used extensively in academic and practitioner short-horizon alpha research.

**Volume-Adjusted Mid Price (VAMP)**
```
vamp = Σ(price_i × qty_i) / Σ(qty_i)   over top 3 levels each side
```
Volume-weighted average of the top three bid and ask levels. More resistant to thin-book manipulation than best-bid/ask mid; captures the true cost of sweeping liquidity.

**Order Book Imbalance (OBI)**
```
obi = (bid_qty − ask_qty) / (bid_qty + ask_qty)    ∈ [−1, 1]
```
Instantaneous signed imbalance at the best level. Rolling mean and stddev tracked over a 20-event window for normalisation.

**Microprice**
```
microprice = (ask_qty × best_bid + bid_qty × best_ask) / (bid_qty + ask_qty)
```
Quantity-weighted mid price — skews toward the thinner side. More accurate predictor of next-trade price than arithmetic mid in limit-order-book markets.

**Trade Flow Imbalance (TFI)**
```
tfi = buy_initiated_trades / (buy_initiated_trades + sell_initiated_trades)
```
Rolling fraction of buy-initiated prints over last 50 trades. Buyer-initiated flow above 0.6 is a strong directional signal on most equity and futures venues.

**Implementation details:**
- `OrderBook` maintains bid (`std::greater<double>`) and ask (`std::less<double>`) `std::map` trees with a 4-field BBO cache (`best_bid`, `best_ask`, `best_bid_qty`, `best_ask_qty`). Signal computation reads only the cache — O(1), no map traversal on the hot path.
- `OrderBook::process()` returns `bool` indicating BBO change. Deep-book modifications skip all signal recomputation; `SignalEngine` updates only `timestamp_ns`.
- `RollingWindow<T,N>` is a fixed-capacity circular buffer (template N, allocated once at construction) with O(1) push, mean, and stddev.

---

### 3 — Anomaly Detector (`lattice_anomaly`)

Four independent detectors — spoofing, layering, cancel spikes, bursts — each producing typed alerts, aggregated into a per-symbol risk score by `SymbolScorer`.

**Spoofing detector (`AnomalyDetector`)**

Large orders placed near the best bid/offer and cancelled unusually quickly are the textbook spoofing pattern. Algorithm:
1. `ADD` with `qty ≥ qty_threshold` (default 1000): insert a `PendingOrder` into a fixed-size open-addressing hash table (`capacity = max_tracked_orders`, must be power of two; allocated once at construction).
2. `CANCEL` of a tracked order: compute `elapsed_ns`, call `WelfordStats::update()`, compute Z-score:
   ```
   z = (elapsed_ns − μ) / σ
   ```
3. If `is_stable()` (count ≥ 2, stddev > 0) and `z < z_score_threshold` (default −2.0): emit `SpoofAlert { order_id, z_score, placed_ns, cancelled_ns }`.

**Layering detector (`LayeringDetector`)**

Detects stacking of multiple large orders on one side within a short time window — a common pre-quote-stuffing pattern. Fires `LayeringAlert` when a configurable count of large same-side orders accumulates within `time_window_ns`.

**Cancel spike detector (`CancelSpikeDetector`)**

Monitors the per-symbol cancel rate within a rolling window. A cancel-to-add ratio spike above a threshold within a short window indicates coordinated order withdrawal — characteristic of quote stuffing or liquidity withdrawal before a directional move.

**Burst detector (`BurstDetector`)**

Tracks ADD event inter-arrival times per symbol. A cluster of ADDs with inter-arrival times significantly below the rolling baseline triggers `BurstAlert` — characteristic of algorithmic flooding or co-located spoofing runs.

**Per-symbol risk scoring (`SymbolScorer`)**

Each alert type increments the symbol's floating-point risk score, capped at 1.0 and decaying exponentially over time. Downstream risk engines query `score(sym)` to gate order submission.

**Welford's online algorithm:**
Running mean and M2 accumulator in 24 bytes. No memory allocation, O(1) per observation, numerically stable for arbitrarily large sample counts. Avoids the catastrophic cancellation that afflicts the naive `Σx²/n − (Σx/n)²` formulation.

**Hash table design:** Fixed-capacity open-addressing vector. `remove_order()` performs Robin Hood rehash of the subsequent probe chain to maintain lookup correctness. `evict_stale()` linear-scans and invalidates slots older than `time_window_ns` — no rehashing on eviction (chains broken only at non-chain-head slots, which is rare in practice since most orders are cancelled before expiry).

---

## Benchmarks

Measured on GCP `c2-standard-8` (Intel Cascade Lake, 3.1 GHz all-core), Ubuntu 22.04, GCC 13.2, `-O3 -march=native -ffast-math`, Release build with LTO.

### SHM IPC — `bench_shm`

| Operation | Latency | Throughput |
|---|---|---|
| `ShmWriter::try_write` | **21.8 ns** | — |
| `ShmReader::try_read` | **22.3 ns** | — |
| Round-trip (write + read, single thread) | **21.5 ns** | — |
| SPSC throughput (producer + consumer threads, `FeedEvent` 96 B) | — | **10.4 M events/s** |

### Signal Engine — `bench_signals`

| Operation | Latency |
|---|---|
| `OrderBook` top-of-book modify | **14.8 ns** |
| `SignalEngine` BBO-changing event (full signal recompute) | **168 ns** |
| `SignalEngine` deep-book modify (fast path — no recompute) | **65.9 ns** |
| Mixed workload (50 % BBO, 50 % deep) | **124 ns** |

The 168 ns BBO path includes: map traversal to update book, BBO cache refresh, OFI delta, VAMP (6-level weighted average), OBI rolling window push, microprice, TFI update.

### Anomaly Detector — `bench_anomaly`

| Operation | Latency |
|---|---|
| `WelfordStats::update` | **8.32 ns** |
| ADD below qty threshold (early exit) | **27 ns** |
| `LayeringDetector::process` | **5.8 ns** |
| `CancelSpikeDetector::process` | **2.96 ns** |
| `BurstDetector::process` | **10.5 ns** |

### Observability — `bench_obs`

| Operation | Latency |
|---|---|
| `LatencyHistogram::record` | **9 ns** |
| `QueueDepthTracker::record` | **18 ns** |

---

## Build & Test

```bash
# Requirements: cmake >= 3.25, ninja, GCC 13+ or Clang 16+, Linux (POSIX shm)
# GoogleTest and Google Benchmark fetched automatically via FetchContent

# Release build (-O3 -march=native -ffast-math, LTO)
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build

# Run all 182 tests
ctest --test-dir build --output-on-failure

# Run individual test binaries
./build/tests/test_shm
./build/tests/test_signals
./build/tests/test_anomaly
./build/tests/test_obs
./build/tests/test_simulator
./build/tests/test_hardening
./build/tests/test_stress

# Run a single named test (GoogleTest filter)
./build/tests/test_anomaly --gtest_filter='AnomalyDetector.FastCancelTriggersAlert'

# Run benchmarks (not registered with CTest — run manually)
./build/benchmarks/bench_shm     --benchmark_min_time=2s
./build/benchmarks/bench_signals --benchmark_min_time=2s
./build/benchmarks/bench_anomaly --benchmark_min_time=2s
./build/benchmarks/bench_obs     --benchmark_min_time=2s

# Debug build (AddressSanitizer + UBSan)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

---

## Project Structure

| Path | Description |
|---|---|
| `include/lattice/feed_event.hpp` | `FeedEvent` struct — byte-for-byte identical to `ultrafast::FeedEvent`, zero-conversion interop |
| `include/lattice/shm/` | `ShmLayout`, `ShmWriter`, `ShmReader`, `ShmChannel` (RAII test wrapper), `ShmError` (9 typed codes) |
| `include/lattice/signals/` | `OrderBook`, `SignalEngine`, `SignalSnapshot`, `RollingWindow`, `FeedDecoder` |
| `include/lattice/anomaly/` | `AnomalyDetector`, `WelfordStats`, `BurstDetector`, `LayeringDetector`, `CancelSpikeDetector`, `SymbolScorer`, alert types |
| `include/lattice/config/` | `LatticeConfig` — all tunables, `load_from_env()`, `load_from_file()`, `validate()` |
| `include/lattice/obs/` | `PipelineStats`, `LatencyHistogram`, `QueueDepthTracker` |
| `include/lattice/recovery/` | `Watchdog` — deadline-based stall detector, kick/is_alive |
| `include/lattice/sim/` | `MarketSimulator` — Poisson arrivals, OU mid-price, log-normal qty, burst episodes |
| `include/lattice/util/` | `Result<T,E>` — `std::expected`-like for C++20 |
| `src/shm/` | Minimal TUs anchoring `lattice_shm` static library (template bodies in `_impl.hpp`) |
| `src/signals/` | `order_book.cpp`, `signal_engine.cpp` |
| `src/anomaly/` | `anomaly_detector.cpp`, `welford_stats.cpp`, detector and scorer TUs |
| `src/obs/` | `pipeline_stats.cpp` |
| `tests/` | `test_shm`, `test_signals`, `test_anomaly`, `test_obs`, `test_simulator`, `test_hardening`, `test_stress` |
| `benchmarks/` | `bench_shm`, `bench_signals`, `bench_anomaly`, `bench_obs`, `bench_simulator` |
| `tools/` | `run_simulation` — end-to-end simulator driver |
| `cmake/` | `CompileOptions.cmake` (HFT compiler flags), `LatticeVersion.cmake` |

---

## Design Decisions

### 1 — `std::atomic_ref` over mutex or `std::atomic`

Mutexes are out of the question for a hot-path IPC ring: a `pthread_mutex_lock` cold call costs 20–100 ns and adds a syscall under contention. `std::atomic<uint64_t>` cannot be used in mmap regions — the standard requires in-place construction of atomic objects, which is undefined on memory not managed by the C++ runtime. `std::atomic_ref<uint64_t>` (C++20) was designed for exactly this: it imposes atomic semantics on existing plain storage. The implementation on x86-64 compiles to a single `MOV` with appropriate fence prefix — identical to `std::atomic`, zero overhead, correct behavior.

### 2 — `acquire` / `release` over `seq_cst`

Sequential consistency (`seq_cst`) imposes a total order across all atomic operations on all threads — implemented on x86-64 via `MFENCE` or locked RMW instructions on every store. For SPSC with exactly one producer and one consumer, the required invariant is narrower: the consumer must see all slot writes that happened-before the matching `write_idx` update, and the producer must see the `read_idx` update before checking fullness. `release` on write, `acquire` on read satisfies both with a single compiler fence on x86-64 (TSO guarantees load-load and store-store ordering in hardware). Benchmarked delta between `seq_cst` and `acq/rel` on the SPSC hot path: ~4 ns per operation.

### 3 — Fixed open-addressing hash table over `std::unordered_map`

`std::unordered_map` allocates a node per insertion — a heap call on every tracked order ADD. At 1000 large orders/second, that is 1000 `malloc` / `free` pairs per second on the hot path, with unpredictable latency spikes under allocator contention. The fixed-capacity open-addressing vector is allocated once at construction: `max_tracked_orders` (default 4096) `PendingOrder` slots, each 32 bytes, 128 KB total. Hash is `order_id & mask` (power-of-two capacity). Probe chain correctness on removal is maintained by Robin Hood rehash of subsequent occupied slots — O(k) where k is the chain length, not O(N).

### 4 — Welford's online algorithm over storing samples

Storing cancel latencies and recomputing mean/stddev from scratch is O(n) space and O(n) time per cancel event. Welford's single-pass algorithm maintains running mean (μ) and M2 accumulator in 24 bytes, updating both in O(1) per sample with no growth in memory or computation as the sample count increases. Critically, it is numerically stable: the naive formulation `E[x²] − E[x]²` suffers catastrophic cancellation when variance is small relative to the mean — precisely the case for cancel latencies measured in nanoseconds with sub-percent variation. Welford avoids this entirely.

### 5 — Zero heap allocation on the hot path

Every data structure that participates in event processing — `ShmLayout` slots, `OrderBook` BBO cache, `RollingWindow` circular buffers, the anomaly detector hash table, `LatencyHistogram` buckets — is sized and allocated at object construction. The `new`/`delete` boundary is crossed zero times per event on the hot path. This eliminates allocator lock contention in multi-threaded configurations and makes worst-case latency bounded and deterministic — a hard requirement for co-located HFT systems where a single outlier latency can result in a stale quote or a missed fill.

---

## Requirements

- **OS:** Linux (POSIX `shm_open`, `mmap`, `CLOCK_MONOTONIC`)
- **Compiler:** GCC 13+ or Clang 16+ with full C++20 support (`std::atomic_ref`, concepts, ranges)
- **Build system:** CMake ≥ 3.25, Ninja (recommended)
- **Dependencies:** GoogleTest 1.15.2 and Google Benchmark 1.9.1 — fetched automatically via `FetchContent`, no manual installation required
- **Upstream:** [ultrafast-feed](https://github.com/hiteshjakhar29/ultrafast-feed) for the AF_XDP feed layer (optional — `lattice-ipc` can consume any `FeedEvent` source)
