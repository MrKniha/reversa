"""ASCII and UTF-16LE string extraction (like the `strings` tool, but address aware)."""
from __future__ import annotations

import re

from reversa.analysis.model import String
from reversa.core.model import Binary

_ASCII = re.compile(rb"[\x20-\x7e\t\r\n]{%d,}")
_UTF16 = re.compile(rb"(?:[\x20-\x7e\t\r\n]\x00){%d,}")
MAX_STRINGS = 200_000


def find_strings(binary: Binary, min_len: int = 4, include_code: bool = False) -> list[String]:
    asc = re.compile(_ASCII.pattern % min_len)
    u16 = re.compile(_UTF16.pattern % min_len)
    out: list[String] = []
    for sec in binary.sections:
        if sec.file_size == 0 or sec.name.startswith(".debug") or (sec.executable and not include_code and binary.format != "RAW"):
            continue
        blob = binary.data[sec.offset:sec.offset + sec.file_size]
        taken: set[int] = set()
        for m in u16.finditer(blob):
            out.append(String(sec.addr + m.start(), m.group().decode("utf-16le"), "utf-16le", len(m.group())))
            taken.update(range(m.start(), m.end()))
        for m in asc.finditer(blob):
            if m.start() in taken:
                continue
            out.append(String(sec.addr + m.start(), m.group().decode("ascii"), "ascii", len(m.group())))
            if len(out) >= MAX_STRINGS:
                break
        if len(out) >= MAX_STRINGS:
            binary.warn(f"Nájdených viac ako {MAX_STRINGS} reťazcov – zobrazujem prvých {MAX_STRINGS}.", None, severity="info")
            break
    out.sort(key=lambda s: s.addr)
    return out
