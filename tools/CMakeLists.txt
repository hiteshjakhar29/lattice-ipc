# tools/CMakeLists.txt — developer / diagnostic utilities
#
# These targets are NOT registered with CTest.  Run them manually:
#   ./build/tools/run_simulation [seconds]

# ── run_simulation ─────────────────────────────────────────────────────────────
add_executable(run_simulation run_simulation.cpp)
target_include_directories(run_simulation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(run_simulation PRIVATE
    lattice_signals
    lattice_anomaly
    lattice_compile_options
)
