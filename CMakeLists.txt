cmake_minimum_required(VERSION 3.25)

project(lattice-ipc
    VERSION      0.1.0
    DESCRIPTION  "Lock-free IPC, microstructure signals, and anomaly detection for HFT"
    HOMEPAGE_URL "https://github.com/example/lattice-ipc"
    LANGUAGES    CXX
)

# ── Platform guard ─────────────────────────────────────────────────────────────
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "lattice-ipc requires Linux (POSIX shm_open, mmap)")
endif()

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Default build type ─────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "CMake build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
message(STATUS "[lattice-ipc] Build type: ${CMAKE_BUILD_TYPE}")

# ── Export compile commands ────────────────────────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── CTest must be included before any subdirectory calls add_test() ───────────
include(CTest)
enable_testing()

# ── Custom CMake modules ───────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompileOptions)
include(LatticeVersion)

# ── FetchContent for third-party deps ─────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    SYSTEM
    FIND_PACKAGE_ARGS NAMES GTest
)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST           OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    SYSTEM
)
set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest benchmark)

# ── Subdirectories ─────────────────────────────────────────────────────────────
add_subdirectory(src/shm)
add_subdirectory(src/signals)
add_subdirectory(src/anomaly)
add_subdirectory(src/obs)
add_subdirectory(tests)
add_subdirectory(benchmarks)

# ── Summary ────────────────────────────────────────────────────────────────────
message(STATUS "[lattice-ipc] Targets: lattice_shm, lattice_signals, lattice_anomaly, lattice_obs")
message(STATUS "[lattice-ipc] Tests:   test_shm, test_signals, test_anomaly, test_obs")
message(STATUS "[lattice-ipc] Bench:   bench_shm, bench_signals, bench_anomaly, bench_obs")
