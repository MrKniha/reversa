"""Cross-reference database (who calls / reads / writes an address).

Modelled after radare2's split between code refs (call/jump) and data refs
(read/write/addr) - see `axt`/`axf` in radare2 - but stored as plain Python
dictionaries keyed by target and source address.
"""
from __future__ import annotations

from collections import defaultdict

from reversa.analysis.model import Xref


class XrefDB:
    def __init__(self) -> None:
        self._to: dict[int, set[Xref]] = defaultdict(set)
        self._from: dict[int, set[Xref]] = defaultdict(set)

    def add(self, frm: int, to: int, kind: str) -> None:
        x = Xref(frm, to, kind)
        self._to[to].add(x)
        self._from[frm].add(x)

    def to(self, addr: int, kinds: tuple[str, ...] | None = None) -> list[Xref]:
        xs = self._to.get(addr, ())
        return sorted((x for x in xs if kinds is None or x.kind in kinds), key=lambda x: (x.frm, x.kind))

    def frm(self, addr: int) -> list[Xref]:
        return sorted(self._from.get(addr, ()), key=lambda x: (x.to, x.kind))

    def count_to(self, addr: int) -> int:
        return len(self._to.get(addr, ()))

    def __len__(self) -> int:
        return sum(len(v) for v in self._to.values())
