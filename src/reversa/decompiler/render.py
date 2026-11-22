"""Render structured IR as C-like pseudocode split into typed, clickable segments."""
from __future__ import annotations

from typing import Callable, Optional

from reversa.decompiler.ir import (AddrOf, Asm, Assign, Bin, Branch, Call, Const, Expr, ExprStmt, Global, Goto,
                                   Load, Return, Ternary, Un, Var)
from reversa.decompiler.structure import If, Jump, Label, While

PREC = {"*": 3, "/": 3, "%": 3, "+": 4, "-": 4, "<<": 5, ">>": 5, "<": 6, "<=": 6, ">": 6, ">=": 6, "u<": 6,
        "u<=": 6, "u>": 6, "u>=": 6, "==": 7, "!=": 7, "&": 8, "^": 9, "|": 10, "&&": 11, "||": 12}
TYPES = {1: "char", 2: "int16_t", 4: "int32_t", 8: "int64_t", 16: "int128_t"}


def ctype(size: int) -> str:
    return TYPES.get(size, "int64_t")


class Renderer:
    """``names``: addr -> display name; ``var``: var key -> display name; ``string``: addr -> text|None."""

    def __init__(self, name_of: Callable[[int], str], var_name: Callable[[str], str],
                 string_at: Callable[[int], Optional[str]], void: bool,
                 is_symbol: Callable[[int], bool] = lambda v: False):
        self.is_symbol = is_symbol
        self.name_of = name_of
        self.var_name = var_name
        self.string_at = string_at
        self.void = void
        self.lines: list[dict] = []
        self.labels: set = set()

    # -- expressions -> segments [(text, kind, addr)] ---------------------
    def expr(self, e: Expr, parent: int = 99) -> list:
        if isinstance(e, Var):
            if e.key.startswith("r:__"):
                return [(e.key[4:], "func", None)]
            return [(self.var_name(e.key), "var", e.key)]
        if isinstance(e, Const):
            if self.is_symbol(e.value):
                return self.expr(AddrOf(e.value), parent)
            v = e.value
            if v >= 1 << 63:
                v -= 1 << 64
            text = str(v) if -256 < v < 256 else (hex(v) if v >= 0 else "-" + hex(-v))
            return [(text, "num", None)]
        if isinstance(e, AddrOf):
            s = self.string_at(e.addr)
            if s is not None:
                lit = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")
                return [('"' + lit + '"', "str", e.addr)]
            return [(self.name_of(e.addr), "func", e.addr)]
        if isinstance(e, Global):
            return [(self.name_of(e.addr), "glob", e.addr)]
        if isinstance(e, Load):
            if isinstance(e.addr, (Const, AddrOf)):
                a = e.addr.value if isinstance(e.addr, Const) else e.addr.addr
                return [(self.name_of(a), "glob", a)]
            ad = e.addr
            if isinstance(ad, Un) and ad.op == "&" and isinstance(ad.a, Var):
                return self.expr(ad.a)
            if isinstance(ad, Bin) and ad.op == "+":
                base, idx = ad.a, ad.b
                if isinstance(idx, (AddrOf, Const)) and not isinstance(base, (AddrOf, Const)):
                    base, idx = idx, base
                bval = base.addr if isinstance(base, AddrOf) else (base.value if isinstance(base, Const) and self.is_symbol(base.value) else None)
                if bval is not None:
                    if isinstance(idx, Bin) and idx.op == "*" and isinstance(idx.b, Const) and idx.b.value == e.size:
                        return [(self.name_of(bval), "glob", bval), ("[", "op", None)] + self.expr(idx.a) + [("]", "op", None)]
                    if e.size == 1:
                        return [(self.name_of(bval), "glob", bval), ("[", "op", None)] + self.expr(idx) + [("]", "op", None)]
            return [("*(" + ctype(e.size) + " *)", "type", None), ("(", "op", None)] + self.expr(e.addr) + [(")", "op", None)]
        if isinstance(e, Un):
            if e.op == "?":
                return [("cond_unknown", "var", None)]
            inner = self.expr(e.a, 2)
            return [(e.op, "op", None)] + inner
        if isinstance(e, Bin):
            p = PREC.get(e.op, 10)
            op = e.op
            left = self.expr(e.a, p)
            right = self.expr(e.b, p - 1 if op in ("+", "*", "&", "|", "^", "&&", "||") else p - 0.5)
            if op.startswith("u"):
                op = op[1:]
                left = [("(unsigned)", "type", None)] + left
            out = left + [(f" {op} ", "op", None)] + right
            if p > parent:
                out = [("(", "op", None)] + out + [(")", "op", None)]
            return out
        if isinstance(e, Call):
            out = self.expr(e.target, 0) if not isinstance(e.target, Var) else [("(*", "op", None)] + self.expr(e.target) + [(")", "op", None)]
            out.append(("(", "op", None))
            for i, a in enumerate(e.args):
                if i:
                    out.append((", ", "op", None))
                out += self.expr(a)
            out.append((")", "op", None))
            return out
        if isinstance(e, Ternary):
            out = self.expr(e.cond, 12) + [(" ? ", "op", None)] + self.expr(e.a, 12) + [(" : ", "op", None)] + self.expr(e.b, 12)
            return [("(", "op", None)] + out + [(")", "op", None)] if parent < 13 else out
        return [(repr(e), "plain", None)]

    # -- statements -------------------------------------------------------
    def line(self, indent: int, segs: list, addr: Optional[int] = None) -> None:
        self.lines.append({"indent": indent, "segs": segs, "addr": addr})

    def stmt(self, s, ind: int) -> None:
        if isinstance(s, Label):
            if s.addr in self.labels:
                self.line(max(0, ind - 1), [(f"label_{s.addr:x}:", "label", s.addr)], s.addr)
            return
        if isinstance(s, Assign):
            dst = self.expr(s.dst)
            src = s.src
            if isinstance(src, Bin) and src.a == s.dst and src.op in ("+", "-", "*", "/", "&", "|", "^", "<<", ">>", "%"):
                if isinstance(src.b, Const) and src.b.value == 1 and src.op in ("+", "-"):
                    self.line(ind, dst + [(src.op * 2 + ";", "op", None)], s.addr)
                    return
                self.line(ind, dst + [(f" {src.op}= ", "op", None)] + self.expr(src.b) + [(";", "op", None)], s.addr)
                return
            self.line(ind, dst + [(" = ", "op", None)] + self.expr(src) + [(";", "op", None)], s.addr)
        elif isinstance(s, ExprStmt):
            self.line(ind, self.expr(s.expr) + [(";", "op", None)], s.addr)
        elif isinstance(s, Return):
            if s.value is None or (self.void and isinstance(s.value, Var)):
                self.line(ind, [("return", "kw", None), (";", "op", None)], s.addr)
            elif self.void and isinstance(s.value, Call):
                self.line(ind, self.expr(s.value) + [(";", "op", None)], s.addr)
                self.line(ind, [("return", "kw", None), (";", "op", None)], s.addr)
            else:
                self.line(ind, [("return ", "kw", None)] + self.expr(s.value) + [(";", "op", None)], s.addr)
        elif isinstance(s, Asm):
            self.line(ind, [("__asm(\"" + s.text.replace('"', "'") + "\");", "asm", None)], s.addr)
        elif isinstance(s, Goto):
            if s.target is None:
                self.line(ind, [("goto ", "kw", None), ("*", "op", None)] + (self.expr(s.expr) if s.expr else []) +
                          [(";", "op", None), ("  // nepriamy skok (napr. switch) – zatiaľ nerozbalený", "comment", None)], s.addr)
            else:
                self.labels.add(s.target)
                self.line(ind, [("goto ", "kw", None), (f"label_{s.target:x}", "label", s.target), (";", "op", None)], s.addr)
        elif isinstance(s, Jump):
            if s.kind == "goto":
                self.line(ind, [("goto ", "kw", None), (f"label_{s.target:x}", "label", s.target), (";", "op", None)], s.addr)
            else:
                self.line(ind, [(s.kind, "kw", None), (";", "op", None)], s.addr)
        elif isinstance(s, If):
            self.line(ind, [("if", "kw", None), (" (", "op", None)] + self.expr(s.cond) + [(") {", "op", None)], s.addr)
            self.body(s.then, ind + 1)
            other = s.other
            while True:
                real = [x for x in other if not isinstance(x, Label) or x.addr in self.labels]
                if len(real) == 1 and isinstance(real[0], If):
                    nested = real[0]
                    self.line(ind, [("} ", "op", None), ("else if", "kw", None), (" (", "op", None)] + self.expr(nested.cond) + [(") {", "op", None)], nested.addr)
                    self.body(nested.then, ind + 1)
                    other = nested.other
                    continue
                break
            if [x for x in other if not isinstance(x, Label)]:
                self.line(ind, [("} ", "op", None), ("else", "kw", None), (" {", "op", None)])
                self.body(other, ind + 1)
            self.line(ind, [("}", "op", None)])
        elif isinstance(s, While):
            if s.do_while:
                self.line(ind, [("do", "kw", None), (" {", "op", None)], s.addr)
                self.body(s.body, ind + 1)
                self.line(ind, [("} ", "op", None), ("while", "kw", None), (" (", "op", None)] + self.expr(s.cond) + [(");", "op", None)])
            else:
                cond = self.expr(s.cond) if s.cond is not None else [("true", "kw", None)]
                self.line(ind, [("while", "kw", None), (" (", "op", None)] + cond + [(") {", "op", None)], s.addr)
                self.body(s.body, ind + 1)
                self.line(ind, [("}", "op", None)])
        elif isinstance(s, Branch):  # only in the linear fallback
            self.line(ind, [("if", "kw", None), (" (", "op", None)] + self.expr(s.cond) + [(") ", "op", None), ("goto ", "kw", None),
                            (f"label_{s.true:x}", "label", s.true), (";", "op", None)], s.addr)
            self.labels.add(s.true)
            self.labels.add(s.false)
            self.line(ind, [("goto ", "kw", None), (f"label_{s.false:x}", "label", s.false), (";", "op", None)], s.addr)

    def body(self, nodes: list, ind: int) -> None:
        for n in nodes:
            self.stmt(n, ind)


def collect_goto_targets(nodes: list, acc: set) -> set:
    for n in nodes:
        if isinstance(n, Jump) and n.kind == "goto":
            acc.add(n.target)
        elif isinstance(n, Goto) and n.target is not None:
            acc.add(n.target)
        elif isinstance(n, Branch):
            acc.update((n.true, n.false))
        elif isinstance(n, If):
            collect_goto_targets(n.then, acc)
            collect_goto_targets(n.other, acc)
        elif isinstance(n, While):
            collect_goto_targets(n.body, acc)
    return acc
