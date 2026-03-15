# lattice-ipc

**C++20 lock-free inter-process communication, microstructure signal engine, and real-time spoofing detector for high-frequency trading.**

Part of a two-project HFT portfolio. [Project A (ultrafast-feed)](https://github.com/example/ultrafast-feed) handles AF\_XDP kernel-bypass packet capture and feed parsing into a `SpscRingBuffer<FeedEvent>`. This project picks up from there: it moves `FeedEvent`s between OS processes via shared memory, computes order book signals in sub-100 ns, and detects spoofing patterns in real time using statistical methods.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Process A — ultrafast-feed                                         │
│                                                                     │
│  AF_XDP ──► FeedHandler::rx_loop()                                  │
│                 │  FeedEvent { inject_ns, receive_ns, payload[64] } │
│                 ▼                                                    │
│           ShmWriter<FeedEvent, 65536>                               │
│                 │  [atomic release store — write_idx]               │
└─────────────────║───────────────────────────────────────────────────┘
                  ║  /dev/shm/lattice_feed  (mmap'd POSIX shm)
┌─────────────────║───────────────────────────────────────────────────┐
│  Process B — lattice-ipc                                            │
│                 ║  [atomic acquire load — write_idx]               │
│           ShmReader<FeedEvent, 65536>                               │
│                 │                                                   │
│        ┌────────┴────────┐                                          │
│        ▼                 ▼                                          │
│  SignalEngine      AnomalyDetector                                  │
│        │                 │                                          │
│   OrderBook         WelfordStats                                    │
│   (BBO cache)      (online Z-score)                                 │
│        │                 │                                          │
│  SignalSnapshot     SpoofAlert                                      │
│  {mid, microprice,  {order_id, z_score,                             │
│   obi, spread}       placed_ns, cancelled_ns}                       │
│        │                 │                                          │
│        ▼                 ▼                                          │
│  downstream risk / strategy / surveillance                          │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Subsystems

### 1 — Lock-Free Shared Memory IPC

`ShmWriter<T,N>` and `ShmReader<T,N>` communicate across OS process boundaries via a POSIX shared memory ring buffer (`shm_open` + `mmap`).

- **`ShmLayout<T,N>`** is the raw in-memory footprint: a 64-byte header (magic sentinel, version, capacity, element size) followed by cache-line-aligned `write_idx`, `read_idx`, and `slots[N]`. Three separate 64-byte cache lines — no false sharing between producer and consumer.
- Indices are plain `uint64_t` accessed via **`std::atomic_ref<uint64_t>`** (C++20) — the correct tool for atomic operations on non-atomic storage, designed for exactly this shared-memory pattern.
- **Monotonically increasing** indices (no modulo waste): all N slots are usable. Fullness is `write_idx − read_idx ≥ N`; slot is `idx & (N−1)`.
- Memory ordering matches `SpscRingBuffer` in ultrafast-feed exactly: `relaxed` self-load, `acquire` cross-index load, `release` store.
- **Writer restart recovery**: writer re-creates the shm region, `memset`s it to zero, and publishes the magic sentinel last (release store). Reader detects the reset via index regression and calls `reattach()`.

### 2 — Microstructure Signal Engine

Processes `FeedEvent`s through a live order book and computes two standard microstructure signals on every best-bid/offer change.

**Order Book Imbalance (OBI)**
```
obi = (bid_qty − ask_qty) / (bid_qty + ask_qty)    ∈ [−1, 1]
```
A positive value indicates buy-side pressure; negative indicates sell-side pressure.

**Microprice**
```
microprice = (ask_qty × best_bid  +  bid_qty × best_ask) / (bid_qty + ask_qty)
```
Quantity-weighted mid price — skews toward the side with less resting liquidity.

**Design:**
- `OrderBook` maintains bid/ask `std::map`s and a **4-field BBO cache** (`best_bid`, `best_ask`, `best_bid_qty`, `best_ask_qty`). Signal computation reads only these four fields — O(1), no map traversal in the hot path.
- `OrderBook::process()` returns `true` only when the BBO changes. Deep-book modifications skip signal recomputation entirely — `SignalEngine` updates only `timestamp_ns` in that case.
- Zero heap allocation after warm-up (new price levels during initialisation only).

### 3 — Real-Time Anomaly Detector (Spoofing Detection)

Detects spoofing: large orders placed near the best bid/offer and cancelled unusually quickly.

**Algorithm:**
1. On `ADD` with `qty ≥ qty_threshold`: insert a `PendingOrder` into a fixed-capacity open-addressing hash table (allocated once at construction — no heap in the hot path).
2. On `CANCEL` of a tracked order: compute `elapsed_ns`, update **Welford's online mean/variance**, compute the Z-score:
   ```
   z = (elapsed_ns − mean) / stddev
   ```
3. If `stats.is_stable()` (≥ 2 samples) and `z < z_score_threshold` (default `−2.0`): emit a `SpoofAlert`.

**Welford's algorithm** maintains running mean and M2 accumulator in 24 bytes — no memory allocation, O(1) per observation, numerically stable for large sample counts.

**Configurable via `AnomalyConfig`:**

| Parameter | Default | Description |
|---|---|---|
| `qty_threshold` | 1000 | Minimum order size to track |
| `time_window_ns` | 500 ms | Evict pending orders older than this |
| `z_score_threshold` | −2.0 | Alert threshold (sigma below mean cancel latency) |
| `max_tracked_orders` | 4096 | Hash table capacity (must be power of two) |

---

## Benchmarks

Measured on GCP `c2-standard-8` (Intel Cascade Lake), GCC 13, `-O3 -march=native`, Release build.

### SHM IPC — `bench_shm`

| Benchmark | Latency | Throughput |
|---|---|---|
| `ShmWriter::try_write` (single) | **21.8 ns** | — |
| `ShmReader::try_read` (single) | **22.3 ns** | — |
| Round-trip (write + read, single thread) | **21.5 ns** | — |
| SPSC throughput (two threads, `FeedEvent` 96 B) | — | **10.4 M events/s** |

### Signal Engine — `bench_signals`

| Benchmark | Latency/op |
|---|---|
| `OrderBook` modify top-of-book | **14.8 ns** |
| `SignalEngine` BBO-changing modify (full recompute) | **53.4 ns** |
| `SignalEngine` deep-book modify (fast path — no recompute) | **12.1 ns** |
| Mixed workload (50% BBO, 50% deep) | **51.6 ns** |

### Anomaly Detector — `bench_anomaly`

| Benchmark | Latency/op |
|---|---|
| `WelfordStats::update` | **8.32 ns** |
| ADD below qty threshold (early exit) | **27 ns** |
| ADD + CANCEL full sequence (large order, stats stable) | **2.7 µs** |
| CANCEL of tracked order + Z-score | **2.6 µs** |

> The 2.6 µs cancel path includes: hash table lookup, Welford update, `sqrt` for stddev (≈15 cycles on Cascade Lake), and Z-score comparison.

---

## Build

```bash
# Requires: cmake >= 3.25, ninja, C++20 compiler (GCC 13+ or Clang 16+)
# GoogleTest and Google Benchmark are fetched automatically via FetchContent.

cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test binary
./build/tests/test_shm
./build/tests/test_signals
./build/tests/test_anomaly

# Run benchmarks (not in CTest — run manually)
./build/benchmarks/bench_shm
./build/benchmarks/bench_signals
./build/benchmarks/bench_anomaly
```

**Debug build** (enables AddressSanitizer + UBSan):
```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

---

## Project Structure

```
lattice-ipc/
├── cmake/
│   ├── CompileOptions.cmake    # HFT compiler flags (-O3 -march=native -ffast-math)
│   └── LatticeVersion.cmake
├── include/lattice/
│   ├── feed_event.hpp          # FeedEvent struct (matches ultrafast-feed layout)
│   ├── shm/                    # ShmLayout, ShmWriter, ShmReader, ShmChannel, ShmError
│   ├── signals/                # OrderBook, SignalEngine, SignalSnapshot, FeedDecoder
│   └── anomaly/                # AnomalyDetector, WelfordStats, PendingOrder, SpoofAlert
├── src/
│   ├── shm/                    # Stub TUs anchoring lattice_shm (templates in _impl.hpp)
│   ├── signals/                # order_book.cpp, signal_engine.cpp
│   └── anomaly/                # welford_stats.cpp, anomaly_detector.cpp
├── tests/                      # test_shm.cpp, test_signals.cpp, test_anomaly.cpp
└── benchmarks/                 # bench_shm.cpp, bench_signals.cpp, bench_anomaly.cpp
```

**Template implementation pattern:** `ShmWriter<T,N>` and `ShmReader<T,N>` are fully defined in `include/lattice/shm/shm_writer_impl.hpp` / `shm_reader_impl.hpp`, included at the end of the corresponding `.hpp` files. The `src/shm/*.cpp` files are minimal TUs that anchor the static library.

---

## FeedEvent Payload Encoding

`lattice-ipc` defines a 28-byte payload encoding for order book events written into `FeedEvent::payload[]`:

| Offset | Type | Field |
|---|---|---|
| 0 | `uint8_t` | Event type: 1=Add, 2=Modify, 3=Cancel, 4=Trade |
| 1 | `uint8_t` | Side: 1=bid, 0=ask |
| 2–7 | — | Reserved |
| 8–15 | `uint64_t` | `order_id` (little-endian) |
| 16–23 | `double` | `price` (IEEE 754 LE) |
| 24–27 | `uint32_t` | `qty` (little-endian) |

The `make_feed_event<FeedEvent>(type, is_bid, order_id, price, qty)` helper in `feed_decoder.hpp` constructs events for testing and simulation.

---

## Relationship to ultrafast-feed

| Concern | ultrafast-feed (Project A) | lattice-ipc (Project B) |
|---|---|---|
| Packet capture | AF\_XDP kernel bypass | — |
| Ring buffer | `SpscRingBuffer<T,N>` (intra-process, stack/heap) | `ShmWriter/Reader<T,N>` (inter-process, mmap) |
| Data type | Raw `FeedEvent` from wire | `FeedEvent` decoded into order book events |
| Output | `SpscRingBuffer<FeedEvent>` | `SignalSnapshot` + `SpoofAlert` |

`lattice::FeedEvent` is byte-for-byte identical to `ultrafast::FeedEvent` — no conversion needed when consuming events produced by ultrafast-feed.
