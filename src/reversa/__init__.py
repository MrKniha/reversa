"""REVERSA - a beginner-friendly open-source reverse engineering platform.

Quick start for scripts::

    import reversa
    prog = reversa.open("samples/bin/login_x64.elf")
    for func in prog.callers_of("strcmp"):
        print(func.name, hex(func.addr))
"""

__version__ = "0.1.0"

__all__ = ["Program", "open", "__version__"]


def __getattr__(name):  # lazy import keeps `import reversa.formats` light
    if name == "open":
        from reversa.api.program import open_program
        return open_program
    if name == "Program":
        from reversa.api.program import Program
        return Program
    raise AttributeError(name)
