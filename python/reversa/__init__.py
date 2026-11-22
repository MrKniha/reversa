"""REVERSA - a beginner-friendly open-source reverse engineering platform.

The analysis engine is written in C++; this package is its scripting API.
Quick start for scripts::

    import reversa
    prog = reversa.open("samples/bin/login_x64.elf")
    for func in prog.callers_of("strcmp"):
        print(func.name, hex(func.addr))
"""
import sys as _sys

if "_reversa" in _sys.builtin_module_names:  # running inside the reversa application
    import _reversa as _core
else:
    from reversa import _reversa as _core

from reversa.api import API_VERSION, FunctionView, Program, XrefView, open_program  # noqa: E402

__version__ = _core.__version__
ReversaError = _core.ReversaError
LoadError = _core.LoadError
ProjectError = _core.ProjectError
AnnotationError = _core.AnnotationError
open = open_program  # noqa: A001 - reversa.open(path) is the documented entry point

__all__ = ["Program", "FunctionView", "XrefView", "open", "open_program", "ReversaError", "LoadError", "ProjectError",
           "AnnotationError", "API_VERSION", "__version__"]
