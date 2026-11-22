"""Search over bytes, strings, instructions, names and pseudocode."""
from __future__ import annotations

import re
from typing import Iterable

from reversa.core.errors import ReversaError

MAX_HITS = 2000


def parse_hex_pattern(pattern: str) -> re.Pattern:
    """'48 8b ?? 05' -> regex. ``??`` matches any byte."""
    toks = pattern.replace(",", " ").split()
    if len(toks) == 1 and len(toks[0]) > 2:
        t = toks[0]
        toks = [t[i:i + 2] for i in range(0, len(t), 2)]
    parts = []
    for t in toks:
        if t in ("??", "?"):
            parts.append(b".")
        else:
            try:
                parts.append(re.escape(bytes([int(t, 16)])))
            except ValueError:
                raise ReversaError(f"'{t}' nie je hexadecimálny bajt. Príklad vzoru: 48 8b ?? 05") from None
    if not parts:
        raise ReversaError("Prázdny vzor bajtov.")
    return re.compile(b"".join(parts), re.S)


def compile_regex(q: str, flags: int = re.I) -> re.Pattern:
    try:
        return re.compile(q, flags)
    except re.error as e:
        raise ReversaError(f"Neplatný regulárny výraz: {e}") from None


def search_bytes(binary, pattern: str) -> list[int]:
    rx = parse_hex_pattern(pattern)
    hits = []
    for sec in binary.sections:
        blob = binary.data[sec.offset:sec.offset + sec.file_size]
        for m in rx.finditer(blob):
            hits.append(sec.addr + m.start())
            if len(hits) >= MAX_HITS:
                return hits
    return hits


def search_text(items: Iterable[tuple[int, str]], q: str, regex: bool = False) -> list[tuple[int, str]]:
    if regex:
        rx = compile_regex(q)
        match = lambda s: rx.search(s) is not None  # noqa: E731
    else:
        ql = q.lower()
        match = lambda s: ql in s.lower()  # noqa: E731
    out = []
    for addr, text in items:
        if match(text):
            out.append((addr, text))
            if len(out) >= MAX_HITS:
                break
    return out
