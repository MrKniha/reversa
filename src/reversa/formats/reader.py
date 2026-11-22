"""Bounds-checked structure reader shared by all format parsers."""
from __future__ import annotations

import struct


class ParseError(Exception):
    def __init__(self, message: str, offset: int):
        super().__init__(f"{message} @0x{offset:x}")
        self.message = message
        self.offset = offset


class Reader:
    def __init__(self, data: bytes, endian: str = "<"):
        self.data = data
        self.endian = endian

    def unpack(self, fmt: str, offset: int) -> tuple:
        size = struct.calcsize(self.endian + fmt)
        if offset < 0 or offset + size > len(self.data):
            raise ParseError(f"štruktúra '{fmt}' presahuje koniec súboru", offset)
        return struct.unpack_from(self.endian + fmt, self.data, offset)

    def u8(self, o: int) -> int:
        return self.unpack("B", o)[0]

    def u16(self, o: int) -> int:
        return self.unpack("H", o)[0]

    def u32(self, o: int) -> int:
        return self.unpack("I", o)[0]

    def u64(self, o: int) -> int:
        return self.unpack("Q", o)[0]

    def bytes(self, o: int, n: int) -> bytes:
        if o < 0 or n < 0 or o + n > len(self.data):
            raise ParseError("blok presahuje koniec súboru", o)
        return self.data[o:o + n]

    def cstr(self, o: int, limit: int = 1024) -> str:
        if o < 0 or o >= len(self.data):
            raise ParseError("reťazec mimo súboru", o)
        end = self.data.find(b"\0", o, o + limit)
        if end < 0:
            end = min(len(self.data), o + limit)
        return self.data[o:end].decode("utf-8", errors="replace")
