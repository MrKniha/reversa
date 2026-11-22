"""AArch64 lifter: capstone instructions -> IR statements (integer subset)."""
from __future__ import annotations

from typing import Optional

from capstone import arm64 as a64

from reversa.decompiler.ir import (AddrOf, Asm, Assign, Bin, Branch, Call, Const, Expr, ExprStmt, Global, Goto,
                                   LiftedBlock, Load, Return, Ternary, Un, Var, binop)
from reversa.decompiler.lift_common import KNOWN_ARGS, cond_from_flags
from reversa.decompiler.passes import uses

ARGS = [f"x{i}" for i in range(8)]
CLOBBER = tuple(f"x{i}" for i in range(19)) + ("x30",)
ARITH = {"add": "+", "sub": "-", "mul": "*", "sdiv": "/", "udiv": "/", "and": "&", "orr": "|", "eor": "^",
         "lsl": "<<", "lsr": ">>", "asr": ">>", "adds": "+", "subs": "-", "ands": "&"}
IGNORE = {"nop", "paciasp", "autiasp", "bti", "hint", "pacibsp", "autibsp", "dmb", "dsb", "isb", "prfm"}
SAVED = {f"x{i}" for i in range(19, 31)} | {"x29", "x30", "fp", "lr"}
SIZE = {"b": 1, "h": 2, "sb": 1, "sh": 2, "sw": 4}


def fam(name: str) -> str:
    if name in ("sp", "wsp"):
        return "sp"
    if name in ("fp",):
        return "x29"
    if name in ("lr",):
        return "x30"
    if name[0] in "wx" and name[1:].isdigit():
        return "x" + name[1:]
    return name


class Arm64Lifter:
    def __init__(self, ctx):
        self.ctx = ctx

    def reg(self, ci, regid: int) -> Expr:
        name = ci.reg_name(regid)
        if name in ("xzr", "wzr"):
            return Const(0)
        size = 4 if name.startswith("w") else 8
        return Var("r:" + fam(name), size)

    def operand(self, ci, op) -> Expr:
        if op.type == a64.ARM64_OP_REG:
            e = self.reg(ci, op.reg)
            if op.shift.type and op.shift.value:
                sh = {1: "<<", 2: ">>", 3: ">>"}.get(op.shift.type, "<<")
                e = binop(sh, e, Const(op.shift.value))
            return e
        if op.type in (a64.ARM64_OP_IMM, a64.ARM64_OP_CIMM):
            v = op.imm
            if op.shift.type == 1 and op.shift.value:
                v <<= op.shift.value
            return self.ctx.const(v, 8)
        return self.mem(ci, op, 8)

    def mem_addr(self, ci, op) -> tuple[Expr, Optional[str]]:
        m = op.mem
        base = fam(ci.reg_name(m.base)) if m.base else None
        if base in ("sp", "x29") and not m.index:
            return Var(f"{'s' if base == 'x29' else 't'}:{m.disp}"), "stack"
        e: Expr = Var("r:" + base) if base else Const(0)
        if m.index:
            idx: Expr = Var("r:" + fam(ci.reg_name(m.index)))
            if op.shift.value:
                idx = Bin("*", idx, Const(1 << op.shift.value))
            e = Bin("+", e, idx)
        return binop("+", e, Const(m.disp)), None

    def mem(self, ci, op, size: int) -> Expr:
        addr, kind = self.mem_addr(ci, op)
        if kind == "stack":
            return Var(addr.key, size)
        return Load(addr, size)

    def access_size(self, mn: str, regname: str) -> int:
        for suf, sz in SIZE.items():
            if mn.endswith(suf) and mn not in ("ldp", "stp"):
                return sz
        return 4 if regname.startswith("w") else 8

    def lift(self, block, insns: list) -> LiftedBlock:
        lb = LiftedBlock(block.start, succs=list(block.succs))
        out = lb.stmts
        flags = None
        consts: dict[str, int] = {}
        for ci in insns:
            mn, ops, a = ci.mnemonic, ci.operands, ci.address
            if mn in IGNORE:
                continue
            try:
                if mn in ("stp", "ldp") and len(ops) >= 3:
                    r1, r2 = fam(ci.reg_name(ops[0].reg)), fam(ci.reg_name(ops[1].reg))
                    base = fam(ci.reg_name(ops[2].mem.base)) if ops[2].type == a64.ARM64_OP_MEM else ""
                    if base in ("sp", "x29") and (r1 in SAVED or r2 in SAVED):
                        continue  # prologue / epilogue register save
                    size = 4 if ci.reg_name(ops[0].reg).startswith("w") else 8
                    addr, kind = self.mem_addr(ci, ops[2])
                    for i, rop in enumerate(ops[:2]):
                        if kind == "stack":
                            m: Expr = Var(f"{addr.key.split(':')[0]}:{int(addr.key.split(':')[1]) + i * size}", size)
                        else:
                            m = Load(binop("+", addr, Const(i * size)), size)
                        r = self.reg(ci, rop.reg)
                        out.append(Assign(m, r, a) if mn == "stp" else Assign(r, m, a))
                    continue
                if mn in ("adrp", "adr"):
                    out.append(Assign(self.reg(ci, ops[0].reg), Const(ops[1].imm) if mn == "adrp" else self.ctx.const(ops[1].imm, 8), a))
                    continue
                if mn in ("mov", "movz", "sxtw", "uxtw", "sxtb", "uxtb", "sxth", "uxth", "fmov") and len(ops) == 2:
                    dst = self.reg(ci, ops[0].reg)
                    if isinstance(dst, Var) and dst.key == "r:x29":
                        continue
                    out.append(Assign(dst, self.operand(ci, ops[1]), a))
                elif mn == "movk":
                    dst = self.reg(ci, ops[0].reg)
                    out.append(Assign(dst, Bin("|", dst, self.operand(ci, ops[1])), a))
                elif mn in ("mvn", "neg") and len(ops) == 2:
                    out.append(Assign(self.reg(ci, ops[0].reg), Un("~" if mn == "mvn" else "-", self.operand(ci, ops[1])), a))
                elif mn in ARITH and len(ops) == 3:
                    dst = self.reg(ci, ops[0].reg)
                    if isinstance(dst, Var) and dst.key == "r:sp":
                        continue
                    x, y = self.operand(ci, ops[1]), self.operand(ci, ops[2])
                    if isinstance(x, Var) and x.key in ("r:sp", "r:x29") and isinstance(y, Const) and mn in ("add", "sub"):
                        off = y.value if mn == "add" else -y.value
                        out.append(Assign(dst, Un("&", Var(f"{'t' if x.key == 'r:sp' else 's'}:{off}")), a))
                        continue
                    out.append(Assign(dst, binop(ARITH[mn], x, y), a))
                    if mn.endswith("s"):
                        flags = ("cmp", x, y) if mn == "subs" and dst != x and dst != y else ("res", dst)
                elif mn in ("smull", "umull", "smulh", "umulh") and len(ops) == 3:
                    prod = Bin("*", self.operand(ci, ops[1]), self.operand(ci, ops[2]))
                    out.append(Assign(self.reg(ci, ops[0].reg), Bin(">>", prod, Const(64)) if mn.endswith("h") else prod, a))
                elif mn in ("madd", "msub") and len(ops) == 4:
                    prod = Bin("*", self.operand(ci, ops[1]), self.operand(ci, ops[2]))
                    acc = self.operand(ci, ops[3])
                    out.append(Assign(self.reg(ci, ops[0].reg), binop("+" if mn == "madd" else "-", acc, prod), a))
                elif mn in ("cmp", "cmn", "tst"):
                    flags = ({"tst": "test"}.get(mn, mn), self.operand(ci, ops[0]), self.operand(ci, ops[1]))
                elif mn.startswith("ldr") or mn.startswith("ldur"):
                    if len(ops) == 2 and ops[1].type == a64.ARM64_OP_IMM:  # literal pool load
                        out.append(Assign(self.reg(ci, ops[0].reg), Global(ops[1].imm, 8), a))
                        continue
                    size = self.access_size(mn, ci.reg_name(ops[0].reg))
                    addr, kind = self.mem_addr(ci, ops[1])
                    src: Expr = Var(addr.key, size) if kind == "stack" else self.global_or_load(addr, size)
                    out.append(Assign(self.reg(ci, ops[0].reg), src, a))
                elif mn.startswith("str") or mn.startswith("stur"):
                    size = self.access_size(mn, ci.reg_name(ops[0].reg))
                    addr, kind = self.mem_addr(ci, ops[1])
                    dst = Var(addr.key, size) if kind == "stack" else self.global_or_load(addr, size)
                    out.append(Assign(dst, self.reg(ci, ops[0].reg), a))
                elif mn == "cset":
                    out.append(Assign(self.reg(ci, ops[0].reg), cond_from_flags(flags, self.cc(ci)), a))
                elif mn == "csel" and len(ops) == 3:
                    out.append(Assign(self.reg(ci, ops[0].reg),
                                      Ternary(cond_from_flags(flags, self.cc(ci)), self.operand(ci, ops[1]), self.operand(ci, ops[2])), a))
                elif mn == "bl":
                    out.append(self.call(ci, AddrOf(ops[0].imm), ops[0].imm, out))
                elif mn == "blr":
                    tgt = self.ctx.call_target_of(a)
                    out.append(self.call(ci, AddrOf(tgt) if tgt is not None else self.reg(ci, ops[0].reg), tgt, out))
                elif mn == "ret":
                    lb.term = Return(Var("r:x0"), a)
                elif mn == "b":
                    tgt = ops[0].imm
                    if tgt in self.ctx.function.tail_calls:
                        lb.term = Return(self.call_expr(AddrOf(tgt), tgt, out), a)
                    else:
                        lb.term = Goto(tgt, a)
                elif mn == "br":
                    tgt = self.ctx.call_target_of(a)
                    if tgt is not None:
                        lb.term = Return(self.call_expr(AddrOf(tgt), tgt, out), a)
                    else:
                        lb.term = Goto(None, a, self.reg(ci, ops[0].reg))
                elif mn.startswith("b."):
                    lb.term = Branch(cond_from_flags(flags, mn[2:]), ops[0].imm, a + 4, a)
                elif mn in ("cbz", "cbnz"):
                    r = self.reg(ci, ops[0].reg)
                    lb.term = Branch(Bin("==" if mn == "cbz" else "!=", r, Const(0)), ops[1].imm, a + 4, a)
                elif mn in ("tbz", "tbnz"):
                    r = self.reg(ci, ops[0].reg)
                    bit = Bin("&", Bin(">>", r, Const(ops[1].imm)), Const(1))
                    lb.term = Branch(Bin("==" if mn == "tbz" else "!=", bit, Const(0)), ops[2].imm, a + 4, a)
                elif mn in ("brk", "udf"):
                    out.append(ExprStmt(Call(Var("r:__trap"), ()), a))
                else:
                    out.append(Asm(f"{mn} {ci.op_str}".strip(), a))
            except Exception as exc:
                out.append(Asm(f"{mn} {ci.op_str}  /* lift error: {type(exc).__name__} */", a))
        return lb

    @staticmethod
    def cc(ci) -> str:
        names = {1: "eq", 2: "ne", 3: "hs", 4: "lo", 5: "mi", 6: "pl", 7: "vs", 8: "vc", 9: "hi", 10: "ls",
                 11: "ge", 12: "lt", 13: "gt", 14: "le"}
        return names.get(ci.cc, "?")

    def global_or_load(self, addr: Expr, size: int) -> Expr:
        if isinstance(addr, Const):
            return Global(addr.value, size)
        return Load(addr, size)

    def call_expr(self, target: Expr, tgt_addr: Optional[int], out: list) -> Call:
        name = self.ctx.callee_name(tgt_addr) if tgt_addr is not None else None
        known = KNOWN_ARGS.get(name) if name else None
        if known is None and tgt_addr is not None:
            known = self.ctx.param_count(tgt_addr)
        assigned = set()
        consumed: set = set()
        for st in reversed(out):
            if isinstance(st, ExprStmt) or (isinstance(st, Assign) and isinstance(st.src, Call)):
                break
            if isinstance(st, Assign) and isinstance(st.dst, Var):
                if st.dst.key not in consumed:
                    assigned.add(st.dst.key[2:])
            consumed |= uses(st)
        n = 0
        while n < 8 and ARGS[n] in assigned:
            n += 1
        if known is not None:
            n = known
        return Call(target, tuple(Var("r:" + r) for r in ARGS[:n]))

    def call(self, ci, target: Expr, tgt_addr: Optional[int], out: list) -> Assign:
        return Assign(Var("r:x0"), self.call_expr(target, tgt_addr, out), ci.address, CLOBBER)
