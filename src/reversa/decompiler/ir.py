"""Tiny expression/statement IR for the pseudocode generator.

Registers become variables named by register *family* (rax/eax/al -> "r:rax");
stack slots become "s:<offset>" variables; outgoing 32-bit stack arguments
become "o:<offset>". The renderer maps these keys to readable names.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Union


class Expr:
    __slots__ = ()


@dataclass(frozen=True)
class Var(Expr):
    key: str  # r:rax | s:-20 | o:4 | p:0
    size: int = 8


@dataclass(frozen=True)
class Const(Expr):
    value: int
    size: int = 8


@dataclass(frozen=True)
class Global(Expr):
    """Memory at a fixed address, e.g. a global variable."""
    addr: int
    size: int = 8


@dataclass(frozen=True)
class AddrOf(Expr):
    """Address of a symbol: function, string or global."""
    addr: int


@dataclass(frozen=True)
class Bin(Expr):
    op: str
    a: Expr
    b: Expr


@dataclass(frozen=True)
class Un(Expr):
    op: str
    a: Expr


@dataclass(frozen=True)
class Load(Expr):
    addr: Expr
    size: int = 8


@dataclass(frozen=True)
class Call(Expr):
    target: Expr
    args: tuple = ()


@dataclass(frozen=True)
class Ternary(Expr):
    cond: Expr
    a: Expr
    b: Expr


class Stmt:
    addr: int = 0


@dataclass
class Assign(Stmt):
    dst: Expr  # Var | Load | Global
    src: Expr
    addr: int = 0
    clobbers: tuple = ()  # registers destroyed (calls)


@dataclass
class ExprStmt(Stmt):
    expr: Expr
    addr: int = 0
    clobbers: tuple = ()


@dataclass
class Asm(Stmt):
    text: str
    addr: int = 0


@dataclass
class Return(Stmt):
    value: Optional[Expr]
    addr: int = 0


@dataclass
class Branch(Stmt):
    cond: Expr
    true: int
    false: int
    addr: int = 0


@dataclass
class Goto(Stmt):
    target: Optional[int]  # None = indirect
    addr: int = 0
    expr: Optional[Expr] = None


Terminator = Union[Return, Branch, Goto, None]


@dataclass
class LiftedBlock:
    start: int
    stmts: list = field(default_factory=list)
    term: Terminator = None
    succs: list = field(default_factory=list)


# ---------------------------------------------------------------- builders
NEG = {"==": "!=", "!=": "==", "<": ">=", ">=": "<", ">": "<=", "<=": ">", "u<": "u>=", "u>=": "u<",
       "u>": "u<=", "u<=": "u>"}


def add(a: Expr, b: Expr) -> Expr:
    if isinstance(b, Const) and b.value == 0:
        return a
    if isinstance(a, Const) and a.value == 0:
        return b
    if isinstance(a, Bin) and a.op == "+" and isinstance(a.b, Const) and isinstance(b, Const):
        return add(a.a, Const(a.b.value + b.value))
    if isinstance(a, Const) and isinstance(b, Const):
        return Const((a.value + b.value) & 0xFFFFFFFFFFFFFFFF)
    if isinstance(b, Const) and b.value < 0:
        return Bin("-", a, Const(-b.value))
    return Bin("+", a, b)


def binop(op: str, a: Expr, b: Expr) -> Expr:
    if op == "+":
        return add(a, b)
    if op == "-" and isinstance(b, Const):
        return add(a, Const(-b.value))
    if op in ("<<", ">>", "|", "^") and isinstance(b, Const) and b.value == 0:
        return a
    if op == "*" and isinstance(b, Const) and b.value == 1:
        return a
    if op == "&" and isinstance(b, Const) and b.value in (1, 0xFF, 0xFFFF, 0xFFFFFFFF) and isinstance(a, Bin) and a.op in NEG:
        return a  # (x == y) & 0xff  ->  x == y
    if op in ("|", "+") and isinstance(a, Const) and isinstance(b, Const):
        return Const((a.value | b.value) if op == "|" else (a.value + b.value))
    if op == "|" and isinstance(a, Bin) and a.op == "|" and isinstance(a.b, Const) and isinstance(b, Const):
        return Bin("|", a.a, Const(a.b.value | b.value))
    return Bin(op, a, b)


def negate(c: Expr) -> Expr:
    if isinstance(c, Bin) and c.op in NEG:
        return Bin(NEG[c.op], c.a, c.b)
    if isinstance(c, Un) and c.op == "!":
        return c.a
    if isinstance(c, Bin) and c.op == "&&":
        return Bin("||", negate(c.a), negate(c.b))
    if isinstance(c, Bin) and c.op == "||":
        return Bin("&&", negate(c.a), negate(c.b))
    return Un("!", c)


def walk(e: Expr):
    """Yield e and all sub-expressions."""
    yield e
    if isinstance(e, Bin):
        yield from walk(e.a)
        yield from walk(e.b)
    elif isinstance(e, Un):
        yield from walk(e.a)
    elif isinstance(e, Load):
        yield from walk(e.addr)
    elif isinstance(e, Call):
        yield from walk(e.target)
        for x in e.args:
            yield from walk(x)
    elif isinstance(e, Ternary):
        yield from walk(e.cond)
        yield from walk(e.a)
        yield from walk(e.b)


def substitute(e: Expr, key: str, rep: Expr) -> Expr:
    if isinstance(e, Var):
        return rep if e.key == key else e
    if isinstance(e, Bin):
        return binop(e.op, substitute(e.a, key, rep), substitute(e.b, key, rep))
    if isinstance(e, Un):
        inner = substitute(e.a, key, rep)
        return negate(inner) if e.op == "!" else Un(e.op, inner)
    if isinstance(e, Load):
        return Load(substitute(e.addr, key, rep), e.size)
    if isinstance(e, Call):
        return Call(substitute(e.target, key, rep), tuple(substitute(x, key, rep) for x in e.args))
    if isinstance(e, Ternary):
        return Ternary(substitute(e.cond, key, rep), substitute(e.a, key, rep), substitute(e.b, key, rep))
    return e
