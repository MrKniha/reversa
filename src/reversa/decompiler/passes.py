"""Data-flow clean-up passes: liveness, expression inlining, dead code removal."""
from __future__ import annotations

from reversa.decompiler.ir import (Assign, Branch, Call, Const, Expr, ExprStmt, Global, Goto, LiftedBlock, Load, Return,
                                   Var, substitute, walk)

TRACKED = ("r:", "o:")


def expr_vars(e) -> set[str]:
    if e is None:
        return set()
    return {x.key for x in walk(e) if isinstance(x, Var)}


def reads_memory(e) -> bool:
    return any(isinstance(x, (Load, Global, Call)) or (isinstance(x, Var) and not x.key.startswith(TRACKED)) for x in walk(e))


def has_call(e) -> bool:
    return any(isinstance(x, Call) for x in walk(e))


def uses(st) -> set[str]:
    if isinstance(st, Assign):
        u = expr_vars(st.src)
        if not isinstance(st.dst, Var):
            u |= expr_vars(st.dst)
        return u
    if isinstance(st, ExprStmt):
        return expr_vars(st.expr)
    if isinstance(st, Return):
        return expr_vars(st.value)
    if isinstance(st, Branch):
        return expr_vars(st.cond)
    if isinstance(st, Goto):
        return expr_vars(st.expr)
    return set()


def defs(st) -> set[str]:
    d = set()
    if isinstance(st, Assign) and isinstance(st.dst, Var):
        d.add(st.dst.key)
    for c in getattr(st, "clobbers", ()) or ():
        d.add("r:" + c)
    return d


def writes_memory(st) -> bool:
    if isinstance(st, Assign):
        return not (isinstance(st.dst, Var) and st.dst.key.startswith(TRACKED)) or has_call(st.src)
    return isinstance(st, ExprStmt)


def all_stmts(lb: LiftedBlock) -> list:
    return lb.stmts + ([lb.term] if lb.term is not None else [])


def liveness(blocks: dict[int, LiftedBlock]) -> tuple[dict[int, set], dict[int, set]]:
    use, df = {}, {}
    for a, lb in blocks.items():
        u, d = set(), set()
        for st in all_stmts(lb):
            u |= {k for k in uses(st) if k.startswith(TRACKED)} - d
            d |= defs(st)
        use[a], df[a] = u, d
    live_in = {a: set() for a in blocks}
    live_out = {a: set() for a in blocks}
    changed = True
    while changed:
        changed = False
        for a in reversed(list(blocks)):
            out = set()
            for s, _ in blocks[a].succs:
                out |= live_in.get(s, set())
            inn = use[a] | (out - df[a])
            if out != live_out[a] or inn != live_in[a]:
                live_out[a], live_in[a] = out, inn
                changed = True
    return live_in, live_out


def _replace_in(st, key: str, rep: Expr):
    if isinstance(st, Assign):
        dst = st.dst if isinstance(st.dst, Var) else substitute(st.dst, key, rep)
        return Assign(dst, substitute(st.src, key, rep), st.addr, st.clobbers)
    if isinstance(st, ExprStmt):
        return ExprStmt(substitute(st.expr, key, rep), st.addr, st.clobbers)
    if isinstance(st, Return):
        return Return(substitute(st.value, key, rep) if st.value is not None else None, st.addr)
    if isinstance(st, Branch):
        return Branch(substitute(st.cond, key, rep), st.true, st.false, st.addr)
    if isinstance(st, Goto):
        return Goto(st.target, st.addr, substitute(st.expr, key, rep) if st.expr is not None else None)
    return st


def _count(e_keys_stmt, key: str) -> int:
    st = e_keys_stmt
    n = 0
    targets = []
    if isinstance(st, Assign):
        targets = [st.src] + ([] if isinstance(st.dst, Var) else [st.dst])
    elif isinstance(st, ExprStmt):
        targets = [st.expr]
    elif isinstance(st, Return):
        targets = [st.value]
    elif isinstance(st, Branch):
        targets = [st.cond]
    elif isinstance(st, Goto):
        targets = [st.expr]
    for t in targets:
        if t is not None:
            n += sum(1 for x in walk(t) if isinstance(x, Var) and x.key == key)
    return n


def simplify_block(lb: LiftedBlock, live_out: set) -> bool:
    """One round of inlining + dead store elimination inside a block."""
    seq = all_stmts(lb)
    changed = False
    i = 0
    while i < len(lb.stmts):
        st = seq[i]
        if not (isinstance(st, Assign) and isinstance(st.dst, Var) and st.dst.key.startswith(TRACKED)):
            i += 1
            continue
        key = st.dst.key
        use_at, n_uses, redefined = None, 0, False
        use_idx = []
        for j in range(i + 1, len(seq)):
            c = _count(seq[j], key)
            if c:
                n_uses += c
                use_idx.append(j)
                use_at = j if use_at is None else use_at
            if key in defs(seq[j]):
                redefined = True
                break
        live_after = (not redefined) and key in live_out
        if n_uses == 0 and not live_after:
            if has_call(st.src):
                seq[i] = ExprStmt(st.src, st.addr, st.clobbers)
                lb.stmts[i] = seq[i]
                i += 1
            else:
                del seq[i]
                del lb.stmts[i]
            changed = True
            continue
        if n_uses >= 1 and not live_after and isinstance(st.src, (Var, Const)) and use_at is not None:
            last = use_idx[-1]
            src_keys = expr_vars(st.src)
            blocked = any(defs(seq[k]) & src_keys for k in range(i + 1, last))
            if not blocked:
                for j in use_idx:
                    new = _replace_in(seq[j], key, st.src)
                    seq[j] = new
                    if j < len(lb.stmts):
                        lb.stmts[j] = new
                    else:
                        lb.term = new
                del seq[i]
                del lb.stmts[i]
                changed = True
                continue
        if n_uses == 1 and not live_after and use_at is not None:
            src_vars = expr_vars(st.src)
            ok = True
            if has_call(st.src) and use_at != i + 1:
                ok = False
            for k in range(i + 1, use_at):
                mid = seq[k]
                if defs(mid) & src_vars:
                    ok = False
                    break
                if reads_memory(st.src) and writes_memory(mid):
                    ok = False
                    break
                if has_call(st.src) and (writes_memory(mid) or has_call(getattr(mid, "src", None) or getattr(mid, "expr", None))):
                    ok = False
                    break
            # do not move a value into a statement that redefines one of its inputs first
            if ok:
                new = _replace_in(seq[use_at], key, st.src)
                seq[use_at] = new
                if use_at < len(lb.stmts):
                    lb.stmts[use_at] = new
                else:
                    lb.term = new
                del seq[i]
                del lb.stmts[i]
                changed = True
                continue
        i += 1
    return changed


def simplify(blocks: dict[int, LiftedBlock], rounds: int = 12) -> None:
    for _ in range(rounds):
        _, live_out = liveness(blocks)
        changed = False
        for a, lb in blocks.items():
            if simplify_block(lb, live_out[a]):
                changed = True
        if not changed:
            break


def returns_value(blocks: dict[int, LiftedBlock], entry: int, ret_key: str) -> bool:
    """True if some path reaches a return with the return register set by a non-call instruction."""
    out_kind: dict[int, set] = {a: set() for a in blocks}
    preds: dict[int, list] = {a: [] for a in blocks}
    for a, lb in blocks.items():
        for s, _ in lb.succs:
            if s in preds:
                preds[s].append(a)
    def last_kind(lb):
        k = None
        seq = all_stmts(lb)
        for i, st in enumerate(lb.stmts):
            if isinstance(st, Assign) and isinstance(st.dst, Var) and st.dst.key == ret_key:
                k = "call" if has_call(st.src) else "val"
                if k == "val" and any(ret_key in uses(x) for x in seq[i + 1:] if not isinstance(x, Return)):
                    k = "used"  # value consumed (e.g. by a compare): a temporary, not a return value
            elif ret_key in defs(st):
                k = "call"
        return k
    kinds = {a: last_kind(lb) for a, lb in blocks.items()}
    changed = True
    in_kind: dict[int, set] = {a: set() for a in blocks}
    while changed:
        changed = False
        for a in blocks:
            inn = set()
            for p in preds[a]:
                inn |= out_kind[p]
            if a == entry:
                inn.add("param")
            out = {kinds[a]} if kinds[a] else inn
            if inn != in_kind[a] or out != out_kind[a]:
                in_kind[a], out_kind[a] = inn, out
                changed = True
    for a, lb in blocks.items():
        if isinstance(lb.term, Return) and lb.term.value is not None and isinstance(lb.term.value, Var):
            if "val" in out_kind[a]:
                return True
    return False
