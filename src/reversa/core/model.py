"""Format-independent description of a loaded binary.

Every format loader (ELF, PE, raw) produces a :class:`Binary`. The analysis
layer only ever talks to this model, never to format specific structures.
"""
from __future__ import annotations

import bisect
import hashlib
from functools import cached_property
from dataclasses import dataclass, field
from typing import Optional

ARCHES = ("x86", "x86-64", "arm", "arm64")


@dataclass
class Section:
    name: str
    addr: int
    size: int
    offset: int
    file_size: int
    readable: bool = True
    writable: bool = False
    executable: bool = False

    @property
    def end(self) -> int:
        return self.addr + self.size

    @property
    def kind(self) -> str:
        if self.executable:
            return "code"
        if self.file_size == 0:
            return "bss"
        return "data"

    @property
    def perms(self) -> str:
        return ("r" if self.readable else "-") + ("w" if self.writable else "-") + ("x" if self.executable else "-")


@dataclass
class Symbol:
    name: str
    addr: int
    size: int = 0
    kind: str = "func"  # func | object | unknown
    source: str = ""  # symtab | dynsym | coff | export | heuristic


@dataclass
class Import:
    name: str
    library: str = ""
    slot: int = 0  # address of the GOT/IAT slot the loader fills in
    thunk: Optional[int] = None  # PLT / jump stub address if one exists
    ordinal: Optional[int] = None


@dataclass
class Export:
    name: str
    addr: int
    ordinal: Optional[int] = None
    forwarder: str = ""


@dataclass
class Relocation:
    addr: int
    type: str
    symbol: str = ""
    addend: int = 0


@dataclass
class Diagnostic:
    """A problem found while loading or analysing. Never fatal by itself."""

    severity: str  # info | warning | error
    message: str
    location: Optional[int] = None  # file offset or address, see ``where``
    where: str = "offset"
    detail: str = ""

    def friendly(self) -> str:
        loc = ""
        if self.location is not None:
            loc = f" ({'adresa' if self.where == 'address' else 'offset v súbore'} 0x{self.location:x})"
        return f"{self.message}{loc}"


@dataclass
class Binary:
    path: str
    data: bytes
    format: str  # ELF | PE | RAW
    arch: str
    bits: int
    endian: str = "little"
    entry: Optional[int] = None
    base: int = 0
    sections: list[Section] = field(default_factory=list)
    symbols: list[Symbol] = field(default_factory=list)
    imports: list[Import] = field(default_factory=list)
    exports: list[Export] = field(default_factory=list)
    relocations: list[Relocation] = field(default_factory=list)
    libraries: list[str] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    compiler: str = "neznámy"
    os: str = ""
    extra_function_starts: list[int] = field(default_factory=list)  # e.g. PE .pdata
    meta: dict = field(default_factory=dict)

    def __post_init__(self) -> None:
        self._index: list[Section] = []
        self._starts: list[int] = []
        self._indexed_count = -1

    # -- address space -------------------------------------------------
    def _mapped(self) -> list[Section]:
        if self._indexed_count != len(self.sections):
            self._index = sorted((s for s in self.sections if s.size > 0), key=lambda s: s.addr)
            self._starts = [s.addr for s in self._index]
            self._indexed_count = len(self.sections)
        return self._index

    def section_at(self, addr: int) -> Optional[Section]:
        secs = self._mapped()
        i = bisect.bisect_right(self._starts, addr) - 1
        # sections may overlap (e.g. ELF .tbss), so look back a little
        for j in range(i, max(i - 4, -1), -1):
            s = secs[j]
            if s.addr <= addr < s.end:
                return s
        return None

    def is_mapped(self, addr: int) -> bool:
        return self.section_at(addr) is not None

    def is_code(self, addr: int) -> bool:
        s = self.section_at(addr)
        return bool(s and s.executable)

    def addr_to_offset(self, addr: int) -> Optional[int]:
        s = self.section_at(addr)
        if s is None or addr - s.addr >= s.file_size:
            return None
        return s.offset + (addr - s.addr)

    def offset_to_addr(self, off: int) -> Optional[int]:
        for s in self._mapped():
            if s.offset <= off < s.offset + s.file_size:
                return s.addr + (off - s.offset)
        return None

    def read(self, addr: int, size: int) -> bytes:
        """Read ``size`` bytes of the loaded image. Unmapped/bss bytes read as zero
        only inside a section; reading stops at the first unmapped address."""
        out = bytearray()
        while size > 0:
            s = self.section_at(addr)
            if s is None:
                break
            rel = addr - s.addr
            chunk = min(size, s.size - rel)
            if rel < s.file_size:
                n = min(chunk, s.file_size - rel)
                start = s.offset + rel
                piece = self.data[start:start + n]
                out += piece
                if len(piece) < n:  # truncated file
                    break
                if n < chunk:
                    out += bytes(chunk - n)
            else:
                out += bytes(chunk)
            addr += chunk
            size -= chunk
        return bytes(out)

    def read_int(self, addr: int, size: int, signed: bool = False) -> Optional[int]:
        b = self.read(addr, size)
        if len(b) != size:
            return None
        return int.from_bytes(b, "little" if self.endian == "little" else "big", signed=signed)

    def read_cstring(self, addr: int, limit: int = 4096) -> Optional[str]:
        b = self.read(addr, limit)
        end = b.find(b"\0")
        if end <= 0:
            return None
        try:
            return b[:end].decode("utf-8")
        except UnicodeDecodeError:
            return b[:end].decode("latin-1")

    @property
    def ptr_size(self) -> int:
        return self.bits // 8

    @cached_property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    def warn(self, message: str, location: Optional[int] = None, detail: str = "", severity: str = "warning") -> None:
        self.diagnostics.append(Diagnostic(severity, message, location, "offset", detail))
