"""Control-flow structuring: CFG -> nested if/else, while, do-while, break/continue.

A simplified version of the "structural analysis" family of algorithms
(Cifuentes, "Reverse Compilation Techniques", 1994; also the idea behind
Ghidra's and RetDec's structurers): loops come from natural loops (back edges
to a dominator), if/else regions end at the immediate post-dominator. Anything
that does not fit becomes a labelled ``goto`` - always correct, never pretty.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from reversa.decompiler.ir import Branch, Expr, Goto, LiftedBlock, Return, negate

EXIT = -1


@dataclass
class If:
    cond: Expr
    then: list
    other: list
    addr: int = 0


@dataclass
class While:
    cond: Optional[Expr]  # None = while (true)
    body: list
    addr: int = 0
    do_while: bool = False


@dataclass
class Jump:
    kind: str  # break | continue | goto
    target: Optional[int] = None
    addr: int = 0


@dataclass
class Label:
    addr: int


@dataclass
class LoopCtx:
    header: int
    body: set
    follow: Optional[int]


@dataclass
class Structurer:
    blocks: dict[int, LiftedBlock]
    entry: int
    loops: dict[int, list]
    ipdom: dict[int, int] = field(default_factory=dict)
    emitted: set = field(default_factory=set)
    labels: set = field(default_factory=set)
    pending: list = field(default_factory=list)

    def run(self) -> list:
        self.ipdom = post_dominators(self.blocks)
        out = self.seq(self.entry, None, None)
        while self.pending:
            n = self.pending.pop(0)
            if n not in self.emitted and n in self.blocks:
                out += self.seq(n, None, None)
        return out

    # ------------------------------------------------------------------
    def loop_follow(self, header: int, body: set) -> Optional[int]:
        exits = []
        for n in sorted(body):
            for s, _ in self.blocks[n].succs:
                if s not in body and s in self.blocks:
                    exits.append(s)
        if not exits:
            return None
        ip = self.ipdom.get(header)
        if ip in exits:
            return ip
        return max(set(exits), key=exits.count)

    def go(self, target: Optional[int], stop: Optional[int], loop: Optional[LoopCtx], out: list) -> Optional[int]:
        """Handle a transfer to ``target``; returns the next node to emit in this sequence or None."""
        if target is None or target == stop:
            return None
        if loop is not None:
            if target == loop.header:
                out.append(Jump("continue"))
                return None
            if target == loop.follow:
                out.append(Jump("break"))
                return None
            if target not in loop.body:
                out.append(Jump("goto", target))
                self.labels.add(target)
                self.pending.append(target)
                return None
        if target in self.emitted or target not in self.blocks:
            out.append(Jump("goto", target))
            self.labels.add(target)
            return None
        return target

    def seq(self, n: Optional[int], stop: Optional[int], loop: Optional[LoopCtx]) -> list:
        out: list = []
        guard = 0
        while n is not None and n != stop:
            guard += 1
            if guard > 10000:
                break
            if n in self.emitted:
                out.append(Jump("goto", n))
                self.labels.add(n)
                break
            if n in self.loops and (loop is None or loop.header != n):
                body = set(self.loops[n])
                ctx = LoopCtx(n, body, self.loop_follow(n, body))
                inner = self.seq(n, None, ctx)
                out.append(make_loop(inner, self.blocks[n].start))
                n = ctx.follow
                if n is not None:
                    n = self.go(n, stop, loop, out)
                continue
            self.emitted.add(n)
            lb = self.blocks[n]
            out.append(Label(n))
            out.extend(lb.stmts)
            t = lb.term
            if isinstance(t, Return) or (isinstance(t, Goto) and t.target is None):
                out.append(t)
                break
            if isinstance(t, Branch):
                merge = self.ipdom.get(n)
                if merge == EXIT:
                    merge = None
                if loop is not None and merge is not None and merge not in loop.body:
                    merge = None
                if merge is not None and merge in self.emitted:
                    merge = None
                then = self.branch(t.true, merge, loop)
                other = self.branch(t.false, merge, loop)
                cond = t.cond
                if not then and other:
                    cond, then, other = negate(cond), other, []
                if then or other:
                    out.append(If(cond, then, other, t.addr))
                n = merge
                if n is not None and loop is not None and n == loop.header:
                    out.append(Jump("continue"))
                    break
                if n is not None and n == stop:
                    break
                continue
            # fall-through / unconditional jump
            succ = t.target if isinstance(t, Goto) else (lb.succs[0][0] if lb.succs else None)
            n = self.go(succ, stop, loop, out)
        return out

    def branch(self, target: int, merge: Optional[int], loop: Optional[LoopCtx]) -> list:
        if target == merge:
            return []
        out: list = []
        nxt = self.go(target, merge, loop, out)
        if nxt is None:
            return out
        return self.seq(nxt, merge, loop)


def make_loop(inner: list, addr: int) -> While:
    body = [x for x in inner]
    # strip trailing continue
    while body and isinstance(body[-1], Jump) and body[-1].kind == "continue":
        body.pop()
    first = next((i for i, x in enumerate(body) if not isinstance(x, Label)), None)
    if first is not None and isinstance(body[first], If):
        head = body[first]
        if head.other and _is(head.other, "break") and not _has_stmts(body[:first]):
            rest = head.then + body[first + 1:]
            while rest and isinstance(rest[-1], Jump) and rest[-1].kind == "continue":
                rest.pop()
            return While(head.cond, body[:first] + rest, addr)
        if _is(head.then, "break") and not head.other and not _has_stmts(body[:first]):
            return While(negate(head.cond), body[:first] + body[first + 1:], addr)
        if _is(head.then, "break") and head.other and not _has_stmts(body[:first]):
            rest = head.other + body[first + 1:]
            while rest and isinstance(rest[-1], Jump) and rest[-1].kind == "continue":
                rest.pop()
            return While(negate(head.cond), body[:first] + rest, addr)
    if body and isinstance(body[-1], If):
        tail = body[-1]
        if _is(tail.then, "break") and not tail.other:
            return While(negate(tail.cond), body[:-1], addr, do_while=True)
        if not tail.then and _is(tail.other, "break"):
            return While(tail.cond, body[:-1], addr, do_while=True)
    return While(None, body, addr)


def _is(lst: list, kind: str) -> bool:
    real = [x for x in lst if not isinstance(x, Label)]
    return len(real) == 1 and isinstance(real[0], Jump) and real[0].kind == kind


def _has_stmts(lst: list) -> bool:
    return any(not isinstance(x, Label) for x in lst)


def post_dominators(blocks: dict[int, LiftedBlock]) -> dict[int, int]:
    nodes = list(blocks)
    succs = {n: [s for s, _ in blocks[n].succs if s in blocks] for n in nodes}
    for n in nodes:
        if not succs[n]:
            succs[n] = [EXIT]
    allnodes = set(nodes) | {EXIT}
    pdom = {n: set(allnodes) for n in nodes}
    pdom[EXIT] = {EXIT}
    changed = True
    while changed:
        changed = False
        for n in reversed(nodes):
            sets = [pdom[s] for s in succs[n] if s in pdom]
            new = (set.intersection(*sets) if sets else set()) | {n}
            if new != pdom[n]:
                pdom[n] = new
                changed = True
    ipdom = {}
    for n in nodes:
        strict = pdom[n] - {n}
        if not strict:
            continue
        ipdom[n] = max(strict, key=lambda p: len(pdom[p]) if p in pdom else 0)
    return ipdom
