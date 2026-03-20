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

# ── test_obs ───────────────────────────────────────────────────────────────────
add_executable(test_obs test_obs.cpp)
target_link_libraries(test_obs PRIVATE lattice_obs lattice_shm GTest::gtest_main)
gtest_discover_tests(test_obs)

# ── test_simulator ─────────────────────────────────────────────────────────────
add_executable(test_simulator test_simulator.cpp)
target_include_directories(test_simulator PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_simulator PRIVATE
    lattice_signals
    lattice_anomaly
    GTest::gtest_main
)
gtest_discover_tests(test_simulator)

# ── test_hardening ─────────────────────────────────────────────────────────────
add_executable(test_hardening test_hardening.cpp)
target_include_directories(test_hardening PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_hardening PRIVATE
    lattice_shm
    GTest::gtest_main
)
gtest_discover_tests(test_hardening)

# ── test_stress ────────────────────────────────────────────────────────────────
add_executable(test_stress test_stress.cpp)
target_include_directories(test_stress PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_stress PRIVATE
    lattice_shm
    lattice_signals
    lattice_anomaly
    GTest::gtest_main
)
gtest_discover_tests(test_stress)
