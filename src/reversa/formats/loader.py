"""Format detection and the single entry point used by the rest of REVERSA."""
from __future__ import annotations

import os
import traceback

from reversa.core.errors import LoadError
from reversa.core.model import Binary
from reversa.formats.elf import is_elf, load_elf
from reversa.formats.pe import is_pe, load_pe
from reversa.formats.raw import load_raw
from reversa.formats.reader import ParseError

MAX_SIZE = 512 * 1024 * 1024


def detect_format(data: bytes) -> str:
    if is_elf(data):
        return "ELF"
    if is_pe(data):
        return "PE"
    if data[:4] in (b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
        return "MACHO"
    if data[:4] == b"dex\n" or data[:2] == b"PK":
        return "ARCHIVE"
    return "RAW"


def load_bytes(data: bytes, path: str = "<memory>", arch: str | None = None, base: int | None = None,
               force_raw: bool = False) -> Binary:
    """Load a binary from bytes. Never raises for malformed content of a recognised
    format: falls back to a raw view and records diagnostics instead."""
    if not data:
        raise LoadError("Súbor je prázdny – nie je čo analyzovať.")
    fmt = "RAW" if force_raw else detect_format(data)
    if fmt in ("ELF", "PE"):
        try:
            b = load_elf(path, data) if fmt == "ELF" else load_pe(path, data)
            if arch:
                b.arch = arch
            return b
        except (ParseError, ValueError, IndexError, KeyError, OverflowError, MemoryError) as exc:
            off = getattr(exc, "offset", None)
            raw = load_raw(path, data, arch, base or 0)
            where = f" na offsete 0x{off:x}" if off is not None else ""
            raw.warn(f"Tento súbor vyzerá ako {fmt}, ale je poškodený{where} – načítal som ho ako surové dáta.",
                     off, "".join(traceback.format_exception(exc)), severity="error")
            return raw
    raw = load_raw(path, data, arch, base or 0)
    if fmt == "MACHO":
        raw.warn("Mach-O (macOS) zatiaľ nie je podporovaný (plánované v plnej verzii) – zobrazujem surové dáta.", 0)
    elif fmt == "ARCHIVE":
        raw.warn("ZIP/APK/DEX zatiaľ nie je podporovaný (plánované v plnej verzii) – zobrazujem surové dáta.", 0)
    return raw


def load_file(path: str, arch: str | None = None, base: int | None = None, force_raw: bool = False) -> Binary:
    if not os.path.exists(path):
        raise LoadError(f"Súbor '{path}' neexistuje.")
    if os.path.isdir(path):
        raise LoadError(f"'{path}' je priečinok, nie súbor.")
    size = os.path.getsize(path)
    if size > MAX_SIZE:
        raise LoadError(f"Súbor má {size // (1024 * 1024)} MB; MVP podporuje najviac {MAX_SIZE // (1024 * 1024)} MB.")
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise LoadError(f"Súbor '{path}' sa nedá prečítať: {exc.strerror}.", str(exc)) from exc
    return load_bytes(data, os.path.abspath(path), arch, base, force_raw)
