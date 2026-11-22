"""Raw binary loader (firmware dumps, shellcode) with optional arch auto-detection."""
from __future__ import annotations

import re

from reversa.core.model import ARCHES, Binary, Section

BITS = {"x86": 32, "x86-64": 64, "arm": 32, "arm64": 64}

# Common function prologues; counting them is a cheap but effective arch guess.
# Inspired by the prologue heuristics in radare2's `aap` and binwalk's `-A` opcode scan.
PROLOGUES = {
    "x86-64": [rb"\x55\x48\x89\xe5", rb"\xf3\x0f\x1e\xfa", rb"\x48\x83\xec.", rb"\x41\x57\x41\x56"],
    "x86": [rb"\x55\x89\xe5", rb"\x55\x8b\xec", rb"\x83\xec.\x56"],
    "arm64": [rb"\xfd\x7b.\xa9", rb"\xfd\x03\x00\x91", rb"\xc0\x03\x5f\xd6"],
    "arm": [rb"[\x00-\xff]{2}\x2d\xe9", rb"\x1e\xff\x2f\xe1"],
}


def guess_arch(data: bytes) -> tuple[str, dict[str, int]]:
    sample = data[: 4 * 1024 * 1024]
    scores = {a: sum(len(re.findall(p, sample, re.S)) for p in pats) for a, pats in PROLOGUES.items()}
    best = max(scores, key=lambda a: scores[a])
    return (best if scores[best] > 0 else "x86-64"), scores


def load_raw(path: str, data: bytes, arch: str | None = None, base: int = 0, endian: str = "little") -> Binary:
    detected = None
    if arch is None:
        arch, scores = guess_arch(data)
        detected = scores
    if arch not in ARCHES:
        raise ValueError(f"Neznáma architektúra '{arch}'. Podporované: {', '.join(ARCHES)}")
    b = Binary(path=path, data=data, format="RAW", arch=arch, bits=BITS[arch], endian=endian, base=base, entry=base)
    b.sections.append(Section("raw", base, len(data), 0, len(data), True, True, True))
    if detected is not None:
        b.meta["arch_scores"] = detected
        b.warn(f"Surové dáta: architektúru som odhadol ako {arch} podľa typických prológov funkcií. "
               "Ak je odhad zlý, zadajte ju ručne (--arch).", None, severity="info")
    return b
