"""Pseudocode generator ("decompiler").

STATUS: provisional (dočasný). It produces readable C-like code for integer
code (functions, calls, if/else, loops, variables instead of registers) on
x86, x86-64 and AArch64. Types are guessed from access sizes only; floating
point, SIMD and jump tables are shown as ``__asm``/``goto *``.
"""
from __future__ import annotations

import traceback
from dataclasses import dataclass, field
from typing import Callable, Optional

from reversa.analysis.graphs import build_cfg
from reversa.analysis.model import Function
from reversa.decompiler.ir import (AddrOf, Assign, Call, Const, ExprStmt, Goto, LiftedBlock, Return, Var, walk)
from reversa.decompiler.lift_arm64 import Arm64Lifter
from reversa.decompiler.lift_x86 import ABI_ARGS, X86Lifter
from reversa.decompiler.passes import returns_value, simplify
from reversa.decompiler.render import Renderer, collect_goto_targets, ctype
from reversa.decompiler.structure import Label, Structurer

PROVISIONAL_NOTE = "// Dočasný pseudokód (REVERSA MVP): typy sú odhadnuté z veľkosti prístupov, overte v listingu."


@dataclass
class Decompiled:
    func: int
    tree: list
    returns: bool
    ret_size: int
    params: list  # var keys in order
    locals: list  # var keys
    sizes: dict
    default_names: dict
    error: str = ""
    labels: set = field(default_factory=set)


class Context:
    def __init__(self, analysis, function: Function):
        self.analysis = analysis
        self.binary = analysis.binary
        self.function = function
        b = self.binary
        if b.arch == "x86-64":
            self.abi = "win64" if b.format == "PE" else "sysv"
        elif b.arch == "x86":
            self.abi = "cdecl"
        else:
            self.abi = "aapcs64"
        self.uses_frame = self._uses_frame()
        self._data_secs = [s for s in b.sections if not s.executable]

    def _uses_frame(self) -> bool:
        if self.binary.arch not in ("x86", "x86-64"):
            return True
        fn = self.function
        blk = fn.blocks.get(fn.addr)
        if not blk:
            return False
        for a in blk.insns[:6]:
            ins = self.analysis.insns.get(a)
            if ins and ins.mnemonic == "mov" and ins.op_str in ("rbp, rsp", "ebp, esp"):
                return True
        return False

    def const(self, v: int, size: int):
        an = self.analysis
        if v in an.string_at or (v in an.functions and v > 0x100):
            return AddrOf(v)
        if v >= 0x1000 and any(s.addr <= v < s.end for s in self._data_secs):
            return AddrOf(v)
        return Const(v, size)

    def call_target_of(self, insn_addr: int) -> Optional[int]:
        for x in self.analysis.xrefs.frm(insn_addr):
            if x.kind in ("call", "jump") and x.to in self.analysis.functions:
                return x.to
        return None

    def callee_name(self, addr: Optional[int]) -> Optional[str]:
        if addr is None:
            return None
        fn = self.analysis.functions.get(addr)
        name = self.analysis.auto_names.get(addr)
        if name is None:
            for s in self.binary.symbols:
                if s.addr == addr:
                    name = s.name
                    break
        if fn is not None and fn.kind == "thunk" and name:
            return name
        return name

    def param_count(self, addr: int) -> Optional[int]:
        return None


def decompile(analysis, func_addr: int) -> Decompiled:
    fn = analysis.functions[func_addr]
    ctx = Context(analysis, fn)
    b = analysis.binary
    lifter = Arm64Lifter(ctx) if b.arch == "arm64" else X86Lifter(ctx)
    ret_key = "r:x0" if b.arch == "arm64" else "r:rax"
    cfg = build_cfg(fn)
    blocks: dict[int, LiftedBlock] = {}
    for start in cfg["nodes"]:
        blk = fn.blocks[start]
        code = b.read(blk.start, blk.end - blk.start)
        insns = list(analysis.disasm.raw(code, blk.start))
        lb = lifter.lift(blk, insns)
        if lb.term is None and len(lb.succs) == 1:
            lb.term = Goto(lb.succs[0][0], insns[-1].address if insns else blk.start)
        blocks[start] = lb
    for lb in blocks.values():  # keep only edges inside the reachable graph
        lb.succs = [(s, k) for s, k in lb.succs if s in blocks]
    rets = returns_value(blocks, cfg["entry"], ret_key)
    ret_sizes = [st.dst.size for lb in blocks.values() for st in lb.stmts
                 if isinstance(st, Assign) and isinstance(st.dst, Var) and st.dst.key == ret_key and not isinstance(st.src, Call)]
    ret_size = max(set(ret_sizes), key=ret_sizes.count) if ret_sizes else 8
    if not rets:
        for lb in blocks.values():
            if isinstance(lb.term, Return) and isinstance(lb.term.value, Var):
                lb.term = Return(None, lb.term.addr)
    split_params(blocks, cfg["entry"], ctx)
    simplify(blocks)
    # ---- variables
    from reversa.decompiler.passes import liveness
    live_in, _ = liveness(blocks)
    entry_live = live_in.get(cfg["entry"], set())
    sizes: dict[str, dict[int, int]] = {}
    for lb in blocks.values():
        for st in lb.stmts + ([lb.term] if lb.term else []):
            for e in _exprs(st):
                for x in walk(e):
                    if isinstance(x, Var):
                        sizes.setdefault(x.key, {}).setdefault(x.size, 0)
                        sizes[x.key][x.size] += 1
    size_of = {k: max(v, key=v.get) for k, v in sizes.items()}
    params: list[str] = []
    regs = arg_regs(ctx)
    if regs:
        keys = []
        for r in regs:
            k = "p:" + r if "p:" + r in size_of else "r:" + r
            keys.append(k)
        live_idx = [i for i, k in enumerate(keys) if k in entry_live or k.startswith("p:")]
        if live_idx:
            params = keys[:max(live_idx) + 1]
            for k in params:
                size_of.setdefault(k, 8)
    else:  # cdecl: positive frame offsets
        offs = sorted({int(k[2:]) for k in size_of if k.startswith("s:") and int(k[2:]) >= 8})
        if offs:
            params = [f"s:{8 + 4 * i}" for i in range((offs[-1] - 8) // 4 + 1)]
    names: dict[str, str] = {}
    for i, k in enumerate(params):
        names[k] = f"a{i + 1}"
    counter = 0
    local_keys = []
    for k in sorted(size_of, key=_var_sort):
        if k in names or k.startswith("p:"):
            continue
        local_keys.append(k)
        kind, _, rest = k.partition(":")
        if kind == "r":
            counter += 1
            names[k] = f"v{counter}"
        elif kind == "s":
            off = int(rest)
            names[k] = f"local_{-off:x}" if off < 0 else f"arg_{off:x}"
        elif kind == "t":
            names[k] = f"stack_{int(rest):x}"
        elif kind == "o":
            names[k] = f"stack_{int(rest):x}"
        elif kind == "g":
            names[k] = rest
    # ---- structure
    error = ""
    try:
        st = Structurer(blocks, cfg["entry"], cfg["loops"])
        tree = st.run()
        labels = collect_goto_targets(tree, set())
    except Exception as exc:  # fall back to a flat listing with gotos
        error = "".join(traceback.format_exception_only(exc)).strip()
        tree = []
        for a, lb in blocks.items():
            tree.append(Label(a))
            tree.extend(lb.stmts)
            if lb.term is not None:
                tree.append(lb.term)
        labels = set(blocks)
    return Decompiled(func_addr, tree, rets, ret_size, params, local_keys, size_of, names, error, labels)


def arg_regs(ctx) -> list[str]:
    if ctx.abi == "aapcs64":
        return [f"x{i}" for i in range(8)]
    return ABI_ARGS.get(ctx.abi, [])


def split_params(blocks: dict, entry: int, ctx) -> None:
    """Give incoming argument registers their own variable ("p:rdi") when every use
    is reached either only by the function entry or only by later definitions."""
    from reversa.decompiler.passes import _replace_in, defs, uses
    preds: dict[int, list] = {a: [] for a in blocks}
    for a, lb in blocks.items():
        for s_, _ in lb.succs:
            if s_ in preds:
                preds[s_].append(a)
    for reg in arg_regs(ctx):
        key = "r:" + reg
        defined = {a: any(key in defs(st) for st in lb.stmts) for a, lb in blocks.items()}
        from_entry = {a: False for a in blocks}
        changed = True
        while changed:
            changed = False
            for a in blocks:
                inn = a == entry or any(from_entry_out(p, from_entry, defined) for p in preds[a])
                if inn != from_entry[a]:
                    from_entry[a], changed = inn, True
        plan, ok = [], True
        for a, lb in blocks.items():
            seen_def = False
            stmts = lb.stmts + ([lb.term] if lb.term is not None else [])
            for i, st in enumerate(stmts):
                if key in uses(st) and not seen_def and from_entry[a]:
                    other = a != entry and any(defined[p] or not from_entry[p] for p in preds[a]) or (a == entry and preds[a])
                    if other:
                        ok = False
                        break
                    plan.append((a, i))
                if key in defs(st):
                    seen_def = True
            if not ok:
                break
        if not ok or not plan:
            continue
        for a, i in plan:
            lb = blocks[a]
            if i < len(lb.stmts):
                st = lb.stmts[i]
                lb.stmts[i] = _replace_in(st, key, Var("p:" + reg, _size_in(st, key)))
            else:
                lb.term = _replace_in(lb.term, key, Var("p:" + reg, _size_in(lb.term, key)))


def from_entry_out(p: int, from_entry: dict, defined: dict) -> bool:
    return from_entry[p] and not defined[p]


def _size_in(st, key: str) -> int:
    for e in _exprs(st):
        for x in walk(e):
            if isinstance(x, Var) and x.key == key:
                return x.size
    return 8


def _exprs(st):
    if isinstance(st, Assign):
        return [st.dst, st.src]
    if isinstance(st, ExprStmt):
        return [st.expr]
    if isinstance(st, Return):
        return [st.value] if st.value is not None else []
    if hasattr(st, "cond"):
        return [st.cond]
    if isinstance(st, Goto) and st.expr is not None:
        return [st.expr]
    return []


def _var_sort(k: str):
    kind, _, rest = k.partition(":")
    order = {"s": 0, "t": 1, "o": 2, "r": 3}.get(kind, 4)
    try:
        return (order, int(rest), k)
    except ValueError:
        return (order, 0, k)


def render(d: Decompiled, func_name: str, name_of: Callable[[int], str], var_override: Callable[[str], Optional[str]],
           string_at: Callable[[int], Optional[str]], is_symbol: Optional[Callable[[int], bool]] = None) -> list[dict]:
    """Render to a list of lines: {indent, segs: [(text, kind, ref)], addr}."""
    def var_name(key: str) -> str:
        return var_override(key) or d.default_names.get(key, key.replace(":", "_"))

    r = Renderer(name_of, var_name, string_at, not d.returns, is_symbol or (lambda v: False))
    r.labels = set(d.labels)
    r.line(0, [(PROVISIONAL_NOTE, "comment", None)])
    if d.error:
        r.line(0, [(f"// Štruktúrovanie zlyhalo ({d.error}) – zobrazujem plochú verziu s goto.", "comment", None)])
    sig = [(("void" if not d.returns else ctype(d.ret_size)) + " ", "type", None), (func_name, "func", d.func), ("(", "op", None)]
    for i, k in enumerate(d.params):
        if i:
            sig.append((", ", "op", None))
        sig += [(ctype(d.sizes.get(k, 8)) + " ", "type", None), (var_name(k), "var", k)]
    sig.append((")", "op", None))
    r.line(0, sig, d.func)
    r.line(0, [("{", "op", None)])
    for k in d.locals:
        if k.startswith("r:rsp") or k.startswith("r:sp") or k.startswith("r:rbp") or k.startswith("r:x29"):
            continue
        r.line(1, [(ctype(d.sizes.get(k, 8)) + " ", "type", None), (var_name(k), "var", k), (";", "op", None)])
    if d.locals:
        r.line(1, [])
    r.body(d.tree, 1)
    r.line(0, [("}", "op", None)])
    return r.lines


def to_text(lines: list[dict]) -> str:
    return "\n".join("    " * ln["indent"] + "".join(s[0] for s in ln["segs"]) for ln in lines)
