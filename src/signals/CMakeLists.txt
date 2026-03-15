add_library(lattice_signals STATIC
    order_book.cpp
    signal_engine.cpp
)

target_include_directories(lattice_signals
    PUBLIC  "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(lattice_signals
    PUBLIC  lattice_compile_options
)
