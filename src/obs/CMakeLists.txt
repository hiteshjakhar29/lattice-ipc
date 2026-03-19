add_library(lattice_obs STATIC
    pipeline_stats.cpp
)

target_include_directories(lattice_obs
    PUBLIC  "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(lattice_obs
    PUBLIC  lattice_compile_options
)
