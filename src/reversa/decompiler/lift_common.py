"""Shared helpers for architecture lifters: flags -> conditions, call conventions."""
from __future__ import annotations

from typing import Optional

from reversa.decompiler.ir import Bin, Const, Expr, Un, binop

# argument counts for common library functions (variadic ones use what was set up)
KNOWN_ARGS = {
    "puts": 1, "strlen": 1, "malloc": 1, "free": 1, "exit": 1, "_exit": 1, "atoi": 1, "strdup": 1, "abort": 0,
    "strcmp": 2, "strcpy": 2, "strcat": 2, "strchr": 2, "strstr": 2, "fopen": 2, "fputs": 2, "calloc": 2,
    "realloc": 2, "signal": 2, "memcpy": 3, "memset": 3, "memcmp": 3, "strncmp": 3, "strncpy": 3, "read": 3,
    "write": 3, "fgets": 3, "fread": 4, "fwrite": 4, "getchar": 0, "rand": 0, "srand": 1, "time": 1,
    "__main": 0, "__stack_chk_fail": 0, "putchar": 1, "fclose": 1, "close": 1, "open": 2, "sleep": 1,
}


def cond_from_flags(flags: Optional[tuple], cc: str) -> Expr:
    """Map a condition code (x86 jcc suffix or ARM64 cond) to a C comparison."""
    if flags is None:
        return Un("?", Const(0))  # unknown flags; rendered as cond_unknown
    kind = flags[0]
    if kind == "cmp":
        a, b = flags[1], flags[2]
    elif kind == "cmn":
        a, b = flags[1], Un("-", flags[2])
    elif kind == "test":
        x, y = flags[1], flags[2]
        a, b = (x if x == y else Bin("&", x, y)), Const(0)
    else:  # result of arithmetic
        a, b = flags[1], Const(0)
    op = {
        "e": "==", "z": "==", "eq": "==", "ne": "!=", "nz": "!=",
        "l": "<", "nge": "<", "lt": "<", "le": "<=", "ng": "<=",
        "g": ">", "nle": ">", "gt": ">", "ge": ">=", "nl": ">=",
        "b": "u<", "nae": "u<", "c": "u<", "lo": "u<", "cc": "u<",
        "be": "u<=", "na": "u<=", "ls": "u<=",
        "a": "u>", "nbe": "u>", "hi": "u>",
        "ae": "u>=", "nb": "u>=", "nc": "u>=", "hs": "u>=", "cs": "u>=",
        "s": "<s0", "mi": "<s0", "ns": ">=s0", "pl": ">=s0",
    }.get(cc)
    if op is None:
        return Un("?", Const(0))
    if op in ("<s0", ">=s0"):
        diff = binop("-", a, b) if not (isinstance(b, Const) and b.value == 0) else a
        return Bin("<" if op == "<s0" else ">=", diff, Const(0))
    return Bin(op, a, b)
