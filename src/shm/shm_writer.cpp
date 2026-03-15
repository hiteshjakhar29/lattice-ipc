// ShmWriter<T,N> is a class template; all implementation lives in
// include/lattice/shm/shm_writer_impl.hpp (included by shm_writer.hpp).
// This translation unit exists to anchor lattice_shm as a STATIC library
// and to provide a single compilation point for any future explicit
// template instantiations.
#include "lattice/shm/shm_writer.hpp"
