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

from reversa.analysis import search as _search
from reversa.core.errors import ReversaError

Ref = Union[int, str]
API_VERSION = "1.0"


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
        return self._p.session.analysis.functions[self.addr]

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
        return [FunctionView(self._p, a) for a in self._p.session.analysis.callers(self.addr)]

    @property
    def callees(self) -> list["FunctionView"]:
        f = self._fn
        return [FunctionView(self._p, a) for a in sorted(f.callees | f.tail_calls) if a in self._p.session.analysis.functions]

    @property
    def xrefs(self) -> list[XrefView]:
        return self._p.xrefs_to(self.addr)

    def instructions(self) -> Iterator:
        a = self._p.session.analysis
        for blk in sorted(self._fn.blocks.values(), key=lambda b: b.start):
            for ad in blk.insns:
                yield a.insns[ad]

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


class Program:
    """Facade over :class:`reversa.session.Session`."""

    api_version = API_VERSION

    def __init__(self, session):
        self.session = session

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
            raise ReversaError(f"'{ref}' som nenašiel (ani ako meno, ani ako adresu).")
        return a

    def name_of(self, addr: int) -> str:
        return self.session.name_of(addr)

    def functions(self, include_imports: bool = False) -> list[FunctionView]:
        a = self.session.require_analysis()
        return [FunctionView(self, f.addr) for f in sorted(a.functions.values(), key=lambda f: f.addr)
                if include_imports or f.kind == "normal"]

    def function(self, ref: Ref) -> FunctionView:
        a = self.session.require_analysis()
        addr = self.addr(ref)
        if addr not in a.functions:
            fn = a.function_containing(addr)
            if fn is None:
                raise ReversaError(f"Na 0x{addr:x} nie je žiadna funkcia.")
            addr = fn.addr
        return FunctionView(self, addr)

    def callers_of(self, ref: Ref) -> list[FunctionView]:
        """Functions that call ``ref`` (a name like 'strcmp' or an address)."""
        a = self.session.require_analysis()
        addr = self.addr(ref)
        return [FunctionView(self, c) for c in a.callers(addr)]

    def xrefs_to(self, ref: Ref) -> list[XrefView]:
        a = self.session.require_analysis()
        addr = self.addr(ref)
        out = []
        for x in a.xrefs.to(addr):
            f = a.function_containing(x.frm)
            out.append(XrefView(x.frm, x.to, x.kind, self.name_of(f.addr) if f else None))
        return out

    def xrefs_from(self, ref: Ref) -> list[XrefView]:
        a = self.session.require_analysis()
        addr = self.addr(ref)
        f = a.function_containing(addr)
        return [XrefView(x.frm, x.to, x.kind, self.name_of(f.addr) if f else None) for x in a.xrefs.frm(addr)]

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
        return self.session.annotations.comments.get(self.addr(ref))

    def bookmark(self, ref: Ref, label: Optional[str]) -> None:
        self.session.set_bookmark(ref, label)

    def undo(self) -> Optional[str]:
        return self.session.undo()

    def redo(self) -> Optional[str]:
        return self.session.redo()

    # -- search
    def search_bytes(self, pattern: str) -> list[int]:
        return _search.search_bytes(self.binary, pattern)

    def search_strings(self, query: str, regex: bool = False) -> list:
        items = [(s.addr, s.text) for s in self.strings()]
        return _search.search_text(items, query, regex)

    def search_instructions(self, query: str, regex: bool = True) -> list:
        a = self.session.require_analysis()
        items = ((ad, a.insns[ad].text) for ad in sorted(a.insns))
        return _search.search_text(items, query, regex)

    def search_pseudocode(self, query: str, regex: bool = True) -> list:
        out = []
        rx = _search.compile_regex(query) if regex else None
        for f in self.functions():
            for line in f.pseudocode().splitlines():
                if (rx.search(line) if rx else query.lower() in line.lower()):
                    out.append((f.addr, line.strip()))
        return out

    # -- views, persistence, reports
    def add_view(self, name: str, render: Callable[["Program"], str]) -> None:
        """Register a custom text/Markdown view; it appears in the GUI under 'Pohľady'."""
        self.session.add_view(name, render)

    def save(self, path: Optional[str] = None) -> str:
        return self.session.save(path)

    def report(self, path: Optional[str] = None, fmt: Optional[str] = None) -> str:
        from reversa.report import build_report
        text = build_report(self.session, fmt or ("html" if path and path.endswith(".html") else "md"))
        if path:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
        return text

    def __repr__(self) -> str:
        return f"<Program {self.path} {self.format} {self.arch}>"


def open_program(path: str, analyze: bool = True, arch: Optional[str] = None, base: Optional[int] = None,
                 raw: bool = False, plugin_dirs: Optional[list[str]] = None, load_plugins: bool = True) -> Program:
    """Open a binary or a .rvproj project and (by default) run auto-analysis."""
    from reversa.session import Session
    s = Session.open(path, arch, base, raw, plugin_dirs, load_plugins)
    prog = Program(s)
    s.plugins._api = prog
    if analyze:
        s.analyze()
    return prog
