// The `reversa._reversa` extension module (used by `import reversa` from a normal Python).
#include "python/bindings.hpp"

PYBIND11_MODULE(_reversa, m) { rv::py_bind::bind_all(m); }
