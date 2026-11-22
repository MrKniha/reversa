"""High-level scripting API used by scripts, plugins and the REPL.

Example::

    import reversa
    prog = reversa.open("samples/bin/login_x64.elf")
    for f in prog.callers_of("strcmp"):
        print(f.name, hex(f.addr))
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Iterator, Optional, Union

Ref = Union[int, str]
API_VERSION = "1.0"


def _core():
    from reversa import _core as core
    return core


@dataclass(frozen=True)
class XrefView:
    frm: int
    to: int
    kind: str  # call | jump | read | write | addr | data
    function: Optional[str]  # name of the function containing ``frm``

    def __repr__(self) -> str:
        return f"<Xref {self.kind} 0x{self.frm:x} -> 0x{self.to:x} in {self.function}>"


class FunctionView:
    """A function, always showing its *current* name (renames propagate)."""

    def __init__(self, program: "Program", addr: int):
        self._p = program
        self.addr = addr

    @property
    def _fn(self):
        return self._p.session.require_analysis().function(self.addr)

    @property
    def name(self) -> str:
        return self._p.session.name_of(self.addr)

    @property
    def size(self) -> int:
        return self._fn.size

    @property
    def kind(self) -> str:
        return self._fn.kind

    @property
    def is_import(self) -> bool:
        return self._fn.kind in ("import", "thunk")

    @property
    def callers(self) -> list["FunctionView"]:
        return [FunctionView(self._p, a) for a in self._p.session.require_analysis().callers(self.addr)]

    @property
    def callees(self) -> list["FunctionView"]:
        a = self._p.session.require_analysis()
        f = self._fn
        return [FunctionView(self._p, x) for x in sorted(set(f.callees) | set(f.tail_calls)) if a.function(x) is not None]

    @property
    def xrefs(self) -> list[XrefView]:
        return self._p.xrefs_to(self.addr)

    def instructions(self) -> Iterator:
        a = self._p.session.require_analysis()
        for ad in self._fn.insn_addrs:
            yield a.insn(ad)

    def pseudocode(self) -> str:
        return self._p.session.pseudocode(self.addr)

    def rename(self, new_name: str) -> None:
        self._p.rename(self.addr, new_name)

    def __repr__(self) -> str:
        return f"<Function {self.name} @0x{self.addr:x} size={self.size}>"

    def __eq__(self, other) -> bool:
        return isinstance(other, FunctionView) and other.addr == self.addr

    def __hash__(self) -> int:
        return hash(self.addr)


class _Events:
    """``session.events``: subscribe(topic, handler(**payload)) returns an unsubscribe function."""

    def __init__(self, core):
        self._core = core

    def subscribe(self, topic: str, handler: Callable[..., None]) -> Callable[[], None]:
        token = self._core.subscribe(topic, handler)
        return lambda: self._core.unsubscribe(token)

    @property
    def revision(self) -> int:
        return self._core.revision


class _Annotations:
    """``session.annotations``: read-only snapshots; change them with Program.rename/comment/bookmark (undoable)."""

    def __init__(self, core):
        self._core = core

    names = property(lambda self: self._core.names)
    comments = property(lambda self: self._core.comments)
    bookmarks = property(lambda self: self._core.bookmarks)
    var_names = property(lambda self: self._core.var_names)


class _Settings(dict):
    """Project settings (saved in the .rvproj); item assignment writes through to the session."""

    def __init__(self, core):
        super().__init__(core.settings or {})
        self._core = core

    def __setitem__(self, key, value) -> None:
        super().__setitem__(key, value)
        self._core.settings = dict(self)

    def __delitem__(self, key) -> None:
        super().__delitem__(key)
        self._core.settings = dict(self)


class _NoPlugins:
    plugins: list = []

    def has_hook(self, hook: str) -> bool:
        return False

    def unload_all(self) -> None:
        pass


class Session:
    """Python view of the C++ session; unknown attributes are forwarded to it."""

    def __init__(self, core):
        self._core = core
        self.events = _Events(core)
        self.annotations = _Annotations(core)
        self.plugins = _NoPlugins()

    def __getattr__(self, name):
        return getattr(self._core, name)

    @property
    def settings(self) -> dict:
        return _Settings(self._core)

    @settings.setter
    def settings(self, value: dict) -> None:
        self._core.settings = dict(value)

    @property
    def dirty(self) -> bool:
        return self._core.dirty

    @dirty.setter
    def dirty(self, value: bool) -> None:
        self._core.dirty = value

    @property
    def views(self) -> dict:
        return {n: (lambda program=None, n=n: self._core.render_view(n)) for n in self._core.view_names}


class Program:
    """Facade over the C++ session (one opened binary plus the user's annotations)."""

    api_version = API_VERSION

    def __init__(self, session):
        self.session = session if isinstance(session, Session) else Session(session)

    @property
    def plugins(self):
        return self.session.plugins

    @plugins.setter
    def plugins(self, manager) -> None:
        self.session.plugins = manager

    # -- info
    @property
    def binary(self):
        return self.session.binary

    @property
    def path(self) -> str:
        return self.binary.path

    @property
    def arch(self) -> str:
        return self.binary.arch

    @property
    def format(self) -> str:
        return self.binary.format

    @property
    def entry(self) -> Optional[int]:
        return self.binary.entry

    @property
    def main(self) -> Optional[FunctionView]:
        m = self.session.require_analysis().main
        return FunctionView(self, m) if m is not None else None

    def analyze(self) -> "Program":
        self.session.analyze()
        return self

    # -- lookup
    def addr(self, ref: Ref) -> int:
        a = self.session.resolve(ref)
        if a is None:
            raise _core().ReversaError(f"'{ref}' som nenašiel (ani ako meno, ani ako adresu).")
        return a

    def name_of(self, addr: int) -> str:
        return self.session.name_of(addr)

    def functions(self, include_imports: bool = False) -> list[FunctionView]:
        a = self.session.require_analysis()
        return [FunctionView(self, addr) for addr, f in sorted(a.functions.items()) if include_imports or f.kind == "normal"]

    def function(self, ref: Ref) -> FunctionView:
        a = self.session.require_analysis()
        addr = self.addr(ref)
        if a.function(addr) is None:
            fn = a.function_containing(addr)
            if fn is None:
                raise _core().ReversaError(f"Na 0x{addr:x} nie je žiadna funkcia.")
            addr = fn.addr
        return FunctionView(self, addr)

    def callers_of(self, ref: Ref) -> list[FunctionView]:
        """Functions that call ``ref`` (a name like 'strcmp' or an address)."""
        a = self.session.require_analysis()
        return [FunctionView(self, c) for c in a.callers(self.addr(ref))]

    def xrefs_to(self, ref: Ref) -> list[XrefView]:
        a = self.session.require_analysis()
        out = []
        for x in a.xrefs_to(self.addr(ref)):
            f = a.function_containing(x.frm)
            out.append(XrefView(x.frm, x.to, x.kind, self.name_of(f.addr) if f else None))
        return out

    def xrefs_from(self, ref: Ref) -> list[XrefView]:
        a = self.session.require_analysis()
        addr = self.addr(ref)
        f = a.function_containing(addr)
        return [XrefView(x.frm, x.to, x.kind, self.name_of(f.addr) if f else None) for x in a.xrefs_from(addr)]

    def imports(self) -> list:
        return list(self.binary.imports)

    def exports(self) -> list:
        return list(self.binary.exports)

    def sections(self) -> list:
        return list(self.binary.sections)

    def strings(self, min_len: int = 4) -> list:
        return [s for s in self.session.require_analysis().strings if len(s.text) >= min_len]

    def read(self, addr: Ref, size: int) -> bytes:
        return self.binary.read(self.addr(addr), size)

    def read_string(self, addr: Ref) -> Optional[str]:
        return self.binary.read_cstring(self.addr(addr))

    def disassemble(self, addr: Ref, count: int = 10) -> list:
        return self.session.require_analysis().decode_at(self.addr(addr), count)

    def decompile(self, ref: Ref) -> str:
        return self.function(ref).pseudocode()

    # -- annotations (undoable)
    def rename(self, ref: Ref, new_name: str) -> str:
        return self.session.rename(ref, new_name)

    def rename_variable(self, func: Ref, var_key: str, new_name: str) -> None:
        self.session.rename_var(func, var_key, new_name)

    def comment(self, ref: Ref, text: Optional[str]) -> None:
        self.session.set_comment(ref, text)

    def get_comment(self, ref: Ref) -> Optional[str]:
        return self.session.comments.get(self.addr(ref))

    def bookmark(self, ref: Ref, label: Optional[str]) -> None:
        self.session.set_bookmark(ref, label)

    def bookmarks(self) -> dict:
        return self.session.bookmarks

    def undo(self) -> Optional[str]:
        return self.session.undo()

    def redo(self) -> Optional[str]:
        return self.session.redo()

    # -- search
    def search_bytes(self, pattern: str) -> list[int]:
        return self.session.search_bytes(pattern)

    def search_strings(self, query: str, regex: bool = False) -> list:
        return self.session.search_strings(query, regex)

    def search_instructions(self, query: str, regex: bool = True) -> list:
        return self.session.search_instructions(query, regex)

    def search_pseudocode(self, query: str, regex: bool = True) -> list:
        return self.session.search_pseudocode(query, regex)

    # -- views, persistence, reports
    def add_view(self, name: str, render: Callable[["Program"], str]) -> None:
        """Register a custom text/Markdown view; it appears in the GUI under 'Pohľady' and in reports."""
        self.session.add_view(name, lambda: str(render(self)))

    def save(self, path: Optional[str] = None) -> str:
        return self.session.save(path)

    def report(self, path: Optional[str] = None, fmt: Optional[str] = None) -> str:
        text = self.session.report(fmt or ("html" if path and path.endswith(".html") else "md"))
        if path:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
        return text

    def __repr__(self) -> str:
        return f"<Program {self.path} {self.format} {self.arch}>"


def open_program(path: str, analyze: bool = True, arch: Optional[str] = None, base: Optional[int] = None,
                 raw: bool = False, plugin_dirs: Optional[list[str]] = None, load_plugins: bool = True) -> Program:
    """Open a binary or a .rvproj project and (by default) run auto-analysis."""
    prog = Program(_core().Session.open(path, arch, base, raw))
    if load_plugins:
        from reversa.plugins import PluginManager
        prog.plugins = PluginManager(prog, plugin_dirs)
        prog.plugins.load_all()
        prog.session.set_plugin_host(prog.plugins)
    if analyze:
        prog.session.analyze()
    return prog
