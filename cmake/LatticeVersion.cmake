# cmake/LatticeVersion.cmake
#
# Exposes LATTICE_VERSION_* variables consumed by ShmLayout's kVersion field
# and potentially by install targets / pkg-config generation.

set(LATTICE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(LATTICE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(LATTICE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(LATTICE_VERSION       "${PROJECT_VERSION}")

message(STATUS "[lattice-ipc] Version: ${LATTICE_VERSION}")
