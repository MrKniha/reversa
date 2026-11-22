"""Control-flow graph (per function) and call graph (whole program)."""
from __future__ import annotations

from typing import Optional

from reversa.analysis.model import Function


def dominators(nodes: list[int], entry: int, preds: dict[int, list[int]]) -> dict[int, set[int]]:
    """Iterative dominator sets (Cooper/Harvey/Kennedy style, set based; fine for MVP sizes)."""
    dom = {n: set(nodes) for n in nodes}
    dom[entry] = {entry}
    changed = True
    while changed:
        changed = False
        for n in nodes:
            if n == entry:
                continue
            ps = [dom[p] for p in preds.get(n, []) if p in dom]
            new = set.intersection(*ps) if ps else set()
            new = new | {n}
            if new != dom[n]:
                dom[n] = new
                changed = True
    return dom


def reachable(fn: Function) -> list[int]:
    seen, order, stack = set(), [], [fn.addr]
    while stack:
        n = stack.pop()
        if n in seen or n not in fn.blocks:
            continue
        seen.add(n)
        order.append(n)
        for s, _ in reversed(fn.blocks[n].succs):
            stack.append(s)
    return order


def build_cfg(fn: Function) -> dict:
    """Return nodes, edges, back edges and natural loops for ``fn``."""
    nodes = reachable(fn) or sorted(fn.blocks)
    nodeset = set(nodes)
    preds: dict[int, list[int]] = {n: [] for n in nodes}
    edges = []
    for n in nodes:
        for s, kind in fn.blocks[n].succs:
            if s in nodeset:
                preds[s].append(n)
                edges.append({"from": n, "to": s, "kind": kind, "back": False})
    entry = fn.addr if fn.addr in nodeset else (nodes[0] if nodes else fn.addr)
    dom = dominators(nodes, entry, preds) if nodes else {}
    loops: dict[int, set[int]] = {}
    for e in edges:
        if e["to"] in dom.get(e["from"], ()):  # edge to a dominator = back edge
            e["back"] = True
            body = {e["to"], e["from"]}
            stack = [e["from"]]
            while stack:
                m = stack.pop()
                for p in preds.get(m, []):
                    if p not in body:
                        body.add(p)
                        stack.append(p)
            loops.setdefault(e["to"], set()).update(body)
    return {
        "entry": entry,
        "nodes": nodes,
        "edges": edges,
        "preds": preds,
        "dom": dom,
        "loops": {h: sorted(b) for h, b in loops.items()},
    }


def call_graph(functions: dict[int, Function], root: Optional[int] = None, depth: int = 0) -> dict:
    """Whole-program call graph, or the neighbourhood of ``root`` up to ``depth`` levels
    (callees and callers) when given - large programs stay readable this way."""
    edges = []
    for f in functions.values():
        for c in sorted(f.callees | f.tail_calls):
            if c in functions:
                edges.append((f.addr, c))
    if root is None or depth <= 0:
        return {"nodes": sorted(functions), "edges": edges}
    keep = {root}
    frontier = {root}
    for _ in range(depth):
        nxt = set()
        for a, b in edges:
            if a in frontier and b not in keep:
                nxt.add(b)
            if b in frontier and a not in keep:
                nxt.add(a)
        keep |= nxt
        frontier = nxt
    return {"nodes": sorted(keep), "edges": [(a, b) for a, b in edges if a in keep and b in keep]}
