add_library(lattice_anomaly STATIC
    welford_stats.cpp
    anomaly_detector.cpp
)

target_include_directories(lattice_anomaly
    PUBLIC  "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(lattice_anomaly
    PUBLIC  lattice_compile_options
)
