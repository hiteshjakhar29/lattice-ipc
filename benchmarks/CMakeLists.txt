# Benchmarks are NOT registered with CTest — run manually.

# ── bench_shm ─────────────────────────────────────────────────────────────────
add_executable(bench_shm bench_shm.cpp)
target_link_libraries(bench_shm PRIVATE lattice_shm benchmark::benchmark_main)

# ── bench_signals ─────────────────────────────────────────────────────────────
add_executable(bench_signals bench_signals.cpp)
target_link_libraries(bench_signals PRIVATE lattice_signals benchmark::benchmark_main)

# ── bench_anomaly ─────────────────────────────────────────────────────────────
add_executable(bench_anomaly bench_anomaly.cpp)
target_link_libraries(bench_anomaly PRIVATE lattice_anomaly benchmark::benchmark_main)

# ── bench_obs ─────────────────────────────────────────────────────────────────
add_executable(bench_obs bench_obs.cpp)
target_link_libraries(bench_obs PRIVATE lattice_obs benchmark::benchmark_main)
