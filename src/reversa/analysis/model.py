"""Analysis result records."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Block:
    start: int
    end: int
    insns: list[int] = field(default_factory=list)  # instruction addresses
    succs: list[tuple[int, str]] = field(default_factory=list)  # (target, kind) kind: true|false|jump|fall


@dataclass
class Function:
    addr: int
    auto_name: str = ""
    kind: str = "normal"  # normal | import | thunk
    blocks: dict[int, Block] = field(default_factory=dict)
    callees: set[int] = field(default_factory=set)
    tail_calls: set[int] = field(default_factory=set)
    size: int = 0
    insn_count: int = 0
    noreturn: bool = False
    library: str = ""

    @property
    def end(self) -> int:
        return self.addr + self.size


@dataclass(frozen=True)
class Xref:
    frm: int
    to: int
    kind: str  # call | jump | read | write | addr | data

    @property
    def label(self) -> str:
        return {"call": "volané z", "jump": "skok z", "read": "čítané z", "write": "zapisované z",
                "addr": "adresa použitá v", "data": "ukazovateľ v dátach"}[self.kind]


@dataclass
class String:
    addr: int
    text: str
    encoding: str  # ascii | utf-16le
    length: int  # bytes in file

    @property
    def end(self) -> int:
        return self.addr + self.length
