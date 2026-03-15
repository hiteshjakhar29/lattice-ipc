# cmake/CompileOptions.cmake
#
# Defines the lattice_compile_options INTERFACE target with HFT-grade
# compiler and linker flags. Every library and executable links this target.

add_library(lattice_compile_options INTERFACE)

target_compile_options(lattice_compile_options INTERFACE
    # ── Always-on diagnostics ──────────────────────────────────────────────────
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -Wno-interference-size

    # Keep frame pointer — lets perf record / perf flamegraph produce useful stacks
    -fno-omit-frame-pointer

    # ── Release: maximum throughput ────────────────────────────────────────────
    $<$<CONFIG:Release>:
        -O3
        -march=native
        -mtune=native
        -DNDEBUG
        -ffast-math
        -funroll-loops
    >

    # ── RelWithDebInfo ─────────────────────────────────────────────────────────
    $<$<CONFIG:RelWithDebInfo>:
        -O2
        -march=native
        -mtune=native
        -g
        -DNDEBUG
    >

    # ── Debug: sanitisers + symbols ────────────────────────────────────────────
    $<$<CONFIG:Debug>:
        -O0
        -g3
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
    >
)

target_link_options(lattice_compile_options INTERFACE
    $<$<CONFIG:Debug>:
        -fsanitize=address,undefined
    >
)

# ── Link Time Optimisation (Release only) ─────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        message(STATUS "[lattice-ipc] LTO enabled for Release build")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "[lattice-ipc] LTO not available: ${_ipo_err}")
    endif()
endif()
