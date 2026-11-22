"""Auto-analysis pipeline: import thunks -> function discovery -> main -> strings -> xrefs.

Function discovery is classic recursive descent (as described in the angr
CFGFast docs and Ghidra's "Disassemble Entry Points" analyzer): start from
known entry points (entry, symbols, exports, PE .pdata), follow calls and
jumps, and split code into basic blocks at branch targets.
"""
from __future__ import annotations

import threading
import time
from typing import Callable, Optional

from reversa.analysis.model import Block, Function, String
from reversa.analysis.strings import find_strings
from reversa.analysis.xrefs import XrefDB
from reversa.core.errors import Cancelled
from reversa.core.model import Binary
from reversa.disasm.base import (FLOW_CALL, FLOW_CJMP, FLOW_ICALL, FLOW_IJMP, FLOW_JMP, FLOW_RET, FLOW_SEQ,
                                 FLOW_TRAP, Insn)
from reversa.disasm.capstone_backend import CapstoneDisassembler, RefTracker

NORETURN = {"exit", "_exit", "_Exit", "abort", "__stack_chk_fail", "__assert_fail", "__libc_start_main",
            "ExitProcess", "ExitThread", "err", "errx", "verr", "verrx", "__cxa_throw", "_assert",
            "__chk_fail", "longjmp", "siglongjmp", "pthread_exit", "quick_exit", "__fortify_fail"}
TERMINATORS = (FLOW_JMP, FLOW_IJMP, FLOW_CJMP, FLOW_RET, FLOW_TRAP)
MAX_FUNC_INSNS = 60_000
DECODE_WINDOW = 512

Progress = Callable[[float, str], None]


class Analysis:
    """Holds every analysis result for one binary. Created by :func:`analyze`."""

    def __init__(self, binary: Binary, disasm: Optional[CapstoneDisassembler] = None):
        self.binary = binary
        self.disasm = disasm or CapstoneDisassembler(binary.arch, binary.endian, binary.is_mapped)
        self.insns: dict[int, Insn] = {}
        self.functions: dict[int, Function] = {}
        self.xrefs = XrefDB()
        self.strings: list[String] = []
        self.string_at: dict[int, String] = {}
        self.auto_names: dict[int, str] = {}
        self.slot_imports: dict[int, str] = {}  # GOT/IAT slot -> import name
        self.import_addr: dict[str, int] = {}  # import name -> address used as its function node
        self.thunk_target: dict[int, int] = {}  # thunk function -> function it jumps to
        self.main: Optional[int] = None
        self.duration = 0.0
        self.done = False
        self._dcache: dict[int, Insn] = {}

    # -- helpers --------------------------------------------------------
    def function_containing(self, addr: int) -> Optional[Function]:
        f = self.functions.get(addr)
        if f:
            return f
        best = None
        for fn in self.functions.values():
            if fn.kind == "normal" and fn.addr <= addr < fn.end:
                for blk in fn.blocks.values():
                    if blk.start <= addr < blk.end:
                        return fn
                best = best or fn
        return best

    def callers(self, addr: int, through_thunks: bool = True) -> list[int]:
        """Functions that call (or tail-jump to) ``addr``. Callers of thunks
        (``strcmp: jmp [__imp_strcmp]``) count as callers of the target."""
        out: set[int] = set()
        todo, seen = [addr], set()
        while todo:
            cur = todo.pop()
            if cur in seen:
                continue
            seen.add(cur)
            for x in self.xrefs.to(cur, ("call", "jump")):
                f = self.function_containing(x.frm)
                if not f or not (x.kind == "call" or cur in f.tail_calls):
                    continue
                if through_thunks and self.thunk_target.get(f.addr) == cur:
                    todo.append(f.addr)
                else:
                    out.add(f.addr)
        return sorted(out)

    def decode_at(self, addr: int, count: int = 1) -> list[Insn]:
        ins = []
        code = self.binary.read(addr, 16 * count + 16)
        for i in self.disasm.decode(code, addr, count):
            ins.append(i)
        return ins


def analyze(binary: Binary, progress: Optional[Progress] = None, cancel: Optional[threading.Event] = None,
            on_function: Optional[Callable[[Function], None]] = None) -> Analysis:
    t0 = time.perf_counter()
    a = Analysis(binary)
    report = progress or (lambda frac, msg: None)

    def check() -> None:
        if cancel is not None and cancel.is_set():
            raise Cancelled()

    report(0.02, "Hľadám importované funkcie")
    _resolve_imports(a)
    report(0.05, "Hľadám funkcie")
    seeds: list[int] = []
    if binary.entry and binary.is_code(binary.entry):
        seeds.append(binary.entry)
        a.auto_names.setdefault(binary.entry, "entry" if binary.format != "RAW" else "start")
    for s in binary.symbols:
        if s.kind == "func" and binary.is_code(s.addr):
            seeds.append(s.addr)
    for e in binary.exports:
        if binary.is_code(e.addr):
            seeds.append(e.addr)
    seeds += [x for x in binary.extra_function_starts if binary.is_code(x)]
    known = set(seeds) | set(a.functions)
    queue = list(dict.fromkeys(seeds))
    done = 0
    while queue:
        check()
        addr = queue.pop(0)
        if addr in a.functions:
            continue
        fn = _analyze_function(a, addr, known)
        a.functions[addr] = fn
        for c in sorted(fn.callees | fn.tail_calls):
            if c not in a.functions and binary.is_code(c) and c not in known:
                known.add(c)
                queue.append(c)
            elif c not in a.functions and binary.is_code(c):
                queue.append(c)
        done += 1
        if done % 25 == 0:
            report(min(0.75, 0.05 + 0.7 * done / (done + len(queue) + 1)), f"Analyzujem funkcie ({done})")
    _mark_thunks(a)
    report(0.78, "Hľadám hlavnú funkciu")
    _find_main(a, known)
    report(0.82, "Hľadám reťazce")
    a.strings = find_strings(binary)
    a.string_at = {s.addr: s for s in a.strings}
    report(0.9, "Počítam cross-referencie")
    _data_pointers(a)
    for fn in a.functions.values():
        if on_function and fn.kind == "normal":
            on_function(fn)
    a.duration = time.perf_counter() - t0
    a.done = True
    report(1.0, "Analýza dokončená")
    return a


# ----------------------------------------------------------------------
def _resolve_imports(a: Analysis) -> None:
    b = a.binary
    for imp in b.imports:
        a.slot_imports[imp.slot] = imp.name
        a.auto_names.setdefault(imp.slot, f"ptr_{imp.name}")
    slots = a.slot_imports
    if b.format == "ELF" and slots:
        for sec in b.sections:
            if not sec.executable or not sec.name.startswith(".plt"):
                continue
            tracker = RefTracker()
            for ins in a.disasm.decode(b.read(sec.addr, sec.size), sec.addr, 0, tracker):
                if ins.flow == FLOW_IJMP and ins.mem_slot in slots:
                    stub = sec.addr + ((ins.addr - sec.addr) & ~0xF)
                    _make_import(a, slots[ins.mem_slot], stub, ins.mem_slot)
                    tracker = RefTracker()
    for imp in b.imports:  # imports reached only through their slot (call [rip+x])
        if imp.name not in a.import_addr:
            _make_import(a, imp.name, imp.slot, imp.slot, imp.library)
        else:
            a.functions[a.import_addr[imp.name]].library = imp.library
            imp.thunk = a.import_addr[imp.name]


def _make_import(a: Analysis, name: str, addr: int, slot: int, library: str = "") -> None:
    if name in a.import_addr:
        return
    a.import_addr[name] = addr
    fn = Function(addr, name, "import" if addr == slot else "thunk", noreturn=name in NORETURN, library=library)
    fn.size = 16 if addr != slot else a.binary.ptr_size
    a.functions[addr] = fn
    a.auto_names[addr] = name


def _call_target(a: Analysis, ins: Insn) -> Optional[int]:
    if ins.target is not None:
        return ins.target
    if ins.mem_slot is not None and ins.mem_slot in a.slot_imports:
        return a.import_addr.get(a.slot_imports[ins.mem_slot])
    return None


def _insn_at(a: Analysis, addr: int) -> Optional[Insn]:
    """Decoded instruction at ``addr``. Decodes a small window at a time and caches
    every instruction, so overlapping functions never decode the same bytes twice
    (keeps analysis of corrupted/raw files near-linear)."""
    ins = a.insns.get(addr) or a._dcache.get(addr)
    if ins is not None:
        return ins
    code = a.binary.read(addr, DECODE_WINDOW)
    if not code:
        return None
    tracker = RefTracker()
    first = None
    for i in a.disasm.decode(code, addr, 0, tracker):
        if first is None:
            first = i
        if i.addr in a._dcache:
            break
        a._dcache[i.addr] = i
        if i.flow in TERMINATORS:
            break
    return first


def _is_noreturn(a: Analysis, target: Optional[int]) -> bool:
    if target is None:
        return False
    fn = a.functions.get(target)
    if fn is not None:
        return fn.noreturn
    return False


def _analyze_function(a: Analysis, start: int, known: set[int]) -> Function:
    b = a.binary
    fn = Function(start)
    local: dict[int, Insn] = {}
    leaders = {start}
    work = [start]
    while work and len(local) < MAX_FUNC_INSNS:
        addr = work.pop()
        if addr in local or not b.is_code(addr):
            continue
        while True:
            ins = _insn_at(a, addr)
            if ins is None or ins.addr in local:
                break
            if ins.addr != start and ins.addr in known and ins.addr in a.functions:
                break  # ran into another function
            local[ins.addr] = ins
            a.insns.setdefault(ins.addr, ins)
            nxt = ins.end
            f = ins.flow
            if f in (FLOW_CALL, FLOW_ICALL):
                tgt = _call_target(a, ins)
                if tgt is not None:
                    fn.callees.add(tgt)
                    a.xrefs.add(ins.addr, tgt, "call")
                if _is_noreturn(a, tgt):
                    break
            elif f == FLOW_CJMP:
                if ins.target is not None and b.is_code(ins.target):
                    a.xrefs.add(ins.addr, ins.target, "jump")
                    leaders.add(ins.target)
                    work.append(ins.target)
                leaders.add(nxt)
                work.append(nxt)
                break
            elif f == FLOW_JMP:
                tgt = ins.target
                if tgt is not None:
                    a.xrefs.add(ins.addr, tgt, "jump")
                    if tgt != start and (tgt in known or tgt in a.functions or not b.is_code(tgt)):
                        fn.tail_calls.add(tgt)
                    else:
                        leaders.add(tgt)
                        work.append(tgt)
                break
            elif f == FLOW_IJMP:
                tgt = _call_target(a, ins)
                if tgt is not None:
                    fn.tail_calls.add(tgt)
                    a.xrefs.add(ins.addr, tgt, "jump")
                break
            elif f in (FLOW_RET, FLOW_TRAP):
                break
            addr = nxt
            if addr in known and addr != start:
                break  # fell through into the next function
            if len(local) >= MAX_FUNC_INSNS:
                break
    for ins in local.values():
        for ref, kind in ins.refs:
            a.xrefs.add(ins.addr, ref, kind)
    # build blocks
    addrs = sorted(local)
    cur: Optional[Block] = None
    for i, ad in enumerate(addrs):
        ins = local[ad]
        if cur is None or ad in leaders or ad != cur.end:
            if cur is not None and ad == cur.end and not cur.succs and local[cur.insns[-1]].flow in (FLOW_SEQ, FLOW_CALL, FLOW_ICALL):
                cur.succs.append((ad, "fall"))
            cur = Block(ad, ad)
            fn.blocks[ad] = cur
        cur.insns.append(ad)
        cur.end = ins.end
        f = ins.flow
        last = (i + 1 == len(addrs)) or (addrs[i + 1] != ins.end) or (addrs[i + 1] in leaders)
        if f == FLOW_CJMP:
            if ins.target is not None and ins.target in local:
                cur.succs.append((ins.target, "true"))
            if ins.end in local:
                cur.succs.append((ins.end, "false"))
            cur = None
        elif f == FLOW_JMP:
            if ins.target in local and ins.target not in fn.tail_calls:
                cur.succs.append((ins.target, "jump"))
            cur = None
        elif f in (FLOW_RET, FLOW_TRAP, FLOW_IJMP):
            cur = None
        elif f in (FLOW_CALL, FLOW_ICALL) and _is_noreturn(a, _call_target(a, ins)):
            cur = None
        elif last and ins.end in local:
            cur.succs.append((ins.end, "fall"))
    fn.insn_count = len(local)
    fn.size = (max(i.end for i in local.values()) - start) if local else 0
    return fn


def _mark_thunks(a: Analysis) -> None:
    """A function whose only instruction is a jump to an import is a thunk."""
    for fn in list(a.functions.values()):
        if fn.kind == "normal" and len(fn.tail_calls) == 1 and fn.insn_count <= 2 and not fn.callees:
            tgt = next(iter(fn.tail_calls))
            target_fn = a.functions.get(tgt)
            if target_fn is not None and target_fn.kind in ("import", "thunk"):
                fn.kind = "thunk"
                fn.noreturn = target_fn.noreturn
                a.thunk_target[fn.addr] = tgt
                a.auto_names.setdefault(fn.addr, target_fn.auto_name)


def _find_main(a: Analysis, known: set[int]) -> None:
    b = a.binary
    for s in b.symbols:
        if s.name in ("main", "_main", "wmain", "WinMain", "wWinMain") and s.addr in a.functions:
            a.main = s.addr
            return
    entry = a.functions.get(b.entry) if b.entry else None
    lsm = a.import_addr.get("__libc_start_main")
    if not entry or lsm is None:
        return
    arg_reg = {"x86-64": "rdi", "arm64": "x0"}.get(b.arch)
    candidate = None
    for ad in sorted(i for blk in entry.blocks.values() for i in blk.insns):
        ins = a.insns[ad]
        if ins.flow in (FLOW_CALL, FLOW_ICALL) and _call_target(a, ins) == lsm:
            break
        if b.arch == "x86" and ins.mnemonic == "push" and ins.refs:
            candidate = ins.refs[0][0]
        elif arg_reg and (ins.op_str.startswith(arg_reg + ",") or ins.op_str.startswith(arg_reg + " ")):
            for ref, kind in ins.refs:
                candidate = _read_pointer(a, ref) if kind == "read" else ref
            if not ins.refs and ins.mnemonic == "mov":
                try:
                    candidate = int(ins.op_str.split(",")[1].strip(), 0)
                except ValueError:
                    pass
    if candidate and b.is_code(candidate):
        a.main = candidate
        a.auto_names[candidate] = "main"
        if candidate not in a.functions:
            known.add(candidate)
            queue = [candidate]
            while queue:
                ad = queue.pop()
                if ad in a.functions or not b.is_code(ad):
                    continue
                fn = _analyze_function(a, ad, known)
                a.functions[ad] = fn
                for c in fn.callees | fn.tail_calls:
                    if c not in a.functions:
                        known.add(c)
                        queue.append(c)


def _read_pointer(a: Analysis, addr: int) -> Optional[int]:
    for r in a.binary.relocations:
        if r.addr == addr and "RELATIVE" in r.type:
            return a.binary.base + r.addend if a.binary.base and r.addend < a.binary.base else r.addend
    return a.binary.read_int(addr, a.binary.ptr_size)


def _data_pointers(a: Analysis) -> None:
    """Pointers stored in data sections (tables of strings, function pointers)."""
    b = a.binary
    ps = b.ptr_size
    rel = {r.addr: r.addend for r in b.relocations if "RELATIVE" in r.type}
    targets = set(a.string_at) | {f for f, fn in a.functions.items() if fn.kind == "normal"}
    for sec in b.sections:
        if sec.executable or sec.file_size == 0 or sec.file_size > 32 * 1024 * 1024:
            continue
        blob = b.data[sec.offset:sec.offset + sec.file_size]
        order = "little" if b.endian == "little" else "big"
        for off in range(0, len(blob) - ps + 1, ps):
            addr = sec.addr + off
            val = rel.get(addr) or int.from_bytes(blob[off:off + ps], order)
            if val in targets:
                a.xrefs.add(addr, val, "data")
