add_library(lattice_shm STATIC
    shm_writer.cpp
    shm_reader.cpp
    shm_channel.cpp
)

target_include_directories(lattice_shm
    PUBLIC  "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(lattice_shm
    PUBLIC  lattice_compile_options
)

# POSIX real-time extensions: shm_open / shm_unlink live in librt on older glibc
target_link_libraries(lattice_shm PUBLIC rt)
