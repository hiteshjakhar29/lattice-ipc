# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

```bash
# Configure (Release — LTO enabled, -O3 -march=native -ffast-math)
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

# Configure (Debug — AddressSanitizer + UBSan)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -G Ninja

# Build all targets
cmake --build build

# Run the full test suite (58 tests across 3 binaries)
ctest --test-dir build --output-on-failure

# Run a single test binary
./build/tests/test_shm
./build/tests/test_signals
./build/tests/test_anomaly

# Run a single named test (GoogleTest filter)
./build/tests/test_anomaly --gtest_filter='AnomalyDetector.FastCancelTriggersAlert'

# Run benchmarks (not in CTest — must be run manually)
./build/benchmarks/bench_shm --benchmark_min_time=2s
./build/benchmarks/bench_signals
./build/benchmarks/bench_anomaly
```

After editing any source file, Ninja detects changes automatically. If not (e.g., after editing a header that CMake doesn't track as a dependency), `touch` the dependent `.cpp` file before rebuilding.

## Architecture

Three static libraries, each independently testable:

**`lattice_shm`** — lock-free SPSC ring buffer over `mmap`'d POSIX shared memory.
- `ShmLayout<T,N>`: raw in-memory footprint stamped at the base of the mmap region. Contains a 64-byte header, then `write_idx` and `read_idx` each on their own `alignas(64)` cache lines, then `slots[N]`.
- Atomicity is via `std::atomic_ref<uint64_t>` on the plain-`uint64_t` indices — correct for shared memory because `std::atomic` is not safe in mmap regions (no in-place construction). Memory ordering: relaxed self-load, acquire cross-load, release store — identical to `SpscRingBuffer` in ultrafast-feed.
- Indices are monotonically increasing (not modulo), so all N slots are usable. Fullness: `write_idx - read_idx >= N`.
- `ShmWriter` owns the shm lifetime (`shm_unlink` in destructor). `ShmReader` attaches by name.
- Template implementations live in `_impl.hpp` files (e.g., `shm_writer_impl.hpp`) included at the bottom of the main header. The `src/shm/*.cpp` files are empty stubs that anchor the STATIC library.

**`lattice_signals`** — order book + microstructure signals.
- `FeedEvent::payload[]` is decoded via `include/lattice/signals/feed_decoder.hpp`. The 28-byte encoding: `[type, side, pad×6, order_id×8, price×8, qty×4]`. `make_feed_event<FeedEvent>(...)` builds test events.
- `OrderBook` uses `std::map<double, uint32_t, std::greater<double>>` for bids (so `begin()` == best bid) and ascending `std::map` for asks. It caches `best_bid_`, `best_ask_`, `best_bid_qty_`, `best_ask_qty_` — `process()` returns `bool` indicating BBO change, enabling `SignalEngine` to skip recomputation on deep-book events.
- `SignalEngine::process()` calls `OrderBook::process()`, then only calls `recompute_signals()` if BBO changed. Otherwise it refreshes only `timestamp_ns`. This is the key fast path.

**`lattice_anomaly`** — Welford Z-score spoofing detector.
- `AnomalyDetector` uses a fixed-size open-addressing hash table (`std::vector<PendingOrder>` sized once in the constructor, capacity = `cfg.max_tracked_orders`, must be power-of-two). The hash is `order_id & mask_`.
- `evict_stale()` linearly scans the table and `invalidate()`s slots older than `time_window_ns` — **no rehashing**. This breaks probe chains if eviction hits a slot in the middle of a chain. In practice this is rare (most orders are cancelled before expiry), but test latencies must be kept below `time_window_ns` (default 500 ms) to avoid evicting an order before its cancel is processed.
- `remove_order()` does proper Robin Hood rehash of the subsequent probe chain to maintain lookup correctness.
- `WelfordStats` guards with `is_stable()` (count ≥ 2) and `stddev() > 0` before computing Z-score. No alert fires until both conditions hold.
- `process_with_time(ev, now_ns, alert)` accepts a caller-supplied timestamp — used in all tests for deterministic behavior.

## Key Design Invariants

- **`ShmLayout<T,N>` requires `T` to be trivially copyable and `N` to be a power of two** — enforced by `static_assert`.
- **`AnomalyConfig::max_tracked_orders` must be a power of two** — asserted at runtime in the constructor.
- **`time_window_ns` in tests**: test cancel latencies must be strictly less than `time_window_ns`. With the default 500 ms window, baseline cancel latencies in tests use 100–200 ms. Using 900 ms+ baselines causes `evict_stale` to remove pending orders before the cancel event is processed, silently dropping stats updates.
- **`lattice::FeedEvent`** in `include/lattice/feed_event.hpp` is byte-for-byte identical to `ultrafast::FeedEvent`. When interoperating with ultrafast-feed, no conversion is needed.
- **`ShmWriter` owns `shm_unlink`** — start the writer process before the reader in production. In tests, `ShmChannel` wraps both endpoints and handles destruction order correctly (reader destroyed before writer).

## CMake Targets

| Target | Type | Links |
|---|---|---|
| `lattice_shm` | STATIC | `lattice_compile_options`, `rt` |
| `lattice_signals` | STATIC | `lattice_compile_options` |
| `lattice_anomaly` | STATIC | `lattice_compile_options` |
| `test_shm` | executable | `lattice_shm`, `GTest::gtest_main` |
| `test_signals` | executable | `lattice_signals`, `GTest::gtest_main` |
| `test_anomaly` | executable | `lattice_anomaly`, `GTest::gtest_main` |
| `bench_shm/signals/anomaly` | executable | `lattice_*`, `benchmark::benchmark_main` |
