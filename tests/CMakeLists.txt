include(GoogleTest)

# ── test_shm ──────────────────────────────────────────────────────────────────
add_executable(test_shm test_shm.cpp)
target_link_libraries(test_shm PRIVATE lattice_shm GTest::gtest_main)
gtest_discover_tests(test_shm)

# ── test_signals ──────────────────────────────────────────────────────────────
add_executable(test_signals test_signals.cpp)
target_link_libraries(test_signals PRIVATE lattice_signals GTest::gtest_main)
gtest_discover_tests(test_signals)

# ── test_anomaly ──────────────────────────────────────────────────────────────
add_executable(test_anomaly test_anomaly.cpp)
target_link_libraries(test_anomaly PRIVATE lattice_anomaly GTest::gtest_main)
gtest_discover_tests(test_anomaly)
