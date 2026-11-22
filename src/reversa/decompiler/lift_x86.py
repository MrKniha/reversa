"""x86 / x86-64 lifter: capstone instructions -> IR statements.

Handles the integer subset that compilers emit for ordinary C code at -O0..-O2.
Anything else (SSE, string ops, ...) is kept verbatim as an ``__asm`` statement
so no information is silently lost.
"""
from __future__ import annotations

from typing import Optional

from capstone import x86

from reversa.decompiler.ir import (AddrOf, Asm, Assign, Bin, Branch, Call, Const, Expr, ExprStmt, Global, Goto,
                                   LiftedBlock, Load, Return, Ternary, Un, Var, binop)
from reversa.decompiler.lift_common import KNOWN_ARGS, cond_from_flags
from reversa.decompiler.passes import uses

FAMILIES = {}
for fam, names in {
    "rax": "rax eax ax al ah", "rbx": "rbx ebx bx bl bh", "rcx": "rcx ecx cx cl ch", "rdx": "rdx edx dx dl dh",
    "rsi": "rsi esi si sil", "rdi": "rdi edi di dil", "rbp": "rbp ebp bp bpl", "rsp": "rsp esp sp spl",
}.items():
    for n in names.split():
        FAMILIES[n] = fam
for i in range(8, 16):
    for suffix in ("", "d", "w", "b"):
        FAMILIES[f"r{i}{suffix}"] = f"r{i}"

ABI_ARGS = {"sysv": ["rdi", "rsi", "rdx", "rcx", "r8", "r9"], "win64": ["rcx", "rdx", "r8", "r9"], "cdecl": []}
CLOBBER = {"sysv": ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"),
           "win64": ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11"), "cdecl": ("rax", "rcx", "rdx")}
CALLEE_SAVED = {"rbx", "rbp", "rsi", "rdi", "r12", "r13", "r14", "r15"}
ARITH = {"add": "+", "sub": "-", "and": "&", "or": "|", "xor": "^", "shl": "<<", "sal": "<<", "shr": ">>",
         "sar": ">>", "imul": "*"}
IGNORE = {"nop", "endbr64", "endbr32", "leave", "cdqe", "cdq", "cqo", "cwde", "cwd", "fwait", "pause", "int3"}


class X86Lifter:
    def __init__(self, ctx):
        self.ctx = ctx
        self.abi = ctx.abi
        self.args = ABI_ARGS[self.abi]
        self.ret = "rax"
        self.bits = ctx.binary.bits

    # -- operands -------------------------------------------------------
    def reg(self, ci, regid: int, size: int = 8) -> Expr:
        name = ci.reg_name(regid)
        fam = FAMILIES.get(name, name)
        return Var("r:" + fam, size)

    def mem_addr(self, ci, op) -> tuple[Expr, Optional[str]]:
        m = op.mem
        if m.segment:
            return Var(f"g:__{ci.reg_name(m.segment)}_{m.disp:x}", op.size), "stack"
        base = ci.reg_name(m.base) if m.base else None
        index = ci.reg_name(m.index) if m.index else None
        if base in ("rip", "eip"):
            return Const(ci.address + ci.size + m.disp), "abs"
        fam = FAMILIES.get(base, base) if base else None
        if fam in ("rbp", "rsp") and index is None and (fam == "rsp" or self.ctx.uses_frame):
            prefix = "s" if fam == "rbp" else ("o" if self.bits == 32 else "t")
            return Var(f"{prefix}:{m.disp}", op.size), "stack"
        e: Optional[Expr] = None
        if base:
            e = Var("r:" + fam)
        if index:
            idx: Expr = Var("r:" + FAMILIES.get(index, index))
            if m.scale != 1:
                idx = Bin("*", idx, Const(m.scale))
            e = idx if e is None else Bin("+", e, idx)
        if e is None:
            return Const(m.disp & ((1 << self.bits) - 1)), "abs"
        disp = self.ctx.const(m.disp, 8) if m.disp >= 0x1000 else Const(m.disp)
        if isinstance(disp, AddrOf):
            return Bin("+", disp, e), None
        return binop("+", e, disp), None

    def operand(self, ci, op) -> Expr:
        if op.type == x86.X86_OP_REG:
            return self.reg(ci, op.reg, op.size)
        if op.type == x86.X86_OP_IMM:
            return self.ctx.const(op.imm, op.size)
        addr, kind = self.mem_addr(ci, op)
        if kind == "stack":
            return addr
        if kind == "abs":
            return Global(addr.value, op.size)
        return Load(addr, op.size)

    def lea(self, ci, op) -> Expr:
        addr, kind = self.mem_addr(ci, op)
        if kind == "stack":
            return Un("&", addr)
        if kind == "abs":
            return self.ctx.const(addr.value, 8)
        return addr

    # -- block ------------------------------------------------------------
    def lift(self, block, insns: list) -> LiftedBlock:
        lb = LiftedBlock(block.start, succs=list(block.succs))
        flags = None
        out = lb.stmts
        for k, ci in enumerate(insns):
            mn = ci.mnemonic
            ops = ci.operands
            a = ci.address
            if mn.startswith(("rep", "bnd ", "notrack ")):
                mn = mn.split()[-1] if mn.startswith(("bnd ", "notrack ")) else mn
            if mn in IGNORE:
                continue
            nxt = insns[k + 1] if k + 1 < len(insns) else None
            try:
                if mn in ("push", "pop"):
                    if ops[0].type == x86.X86_OP_REG and FAMILIES.get(ci.reg_name(ops[0].reg)) in CALLEE_SAVED:
                        continue
                    if mn == "push" and self.bits == 32:
                        continue  # argument pushes are rare with modern GCC/Clang/MSVC -O0; ignored
                    out.append(Asm(f"{ci.mnemonic} {ci.op_str}", a))
                elif mn in ("mov", "movabs", "movzx", "movsx", "movsxd"):
                    dst, src = ops[0], ops[1]
                    if dst.type == x86.X86_OP_REG and ci.reg_name(dst.reg) in ("rbp", "ebp") and src.type == x86.X86_OP_REG:
                        continue  # mov rbp, rsp (frame setup)
                    if (self.abi == "sysv" and dst.type == x86.X86_OP_REG and ci.reg_name(dst.reg) in ("eax", "al", "rax")
                            and src.type == x86.X86_OP_IMM and nxt is not None and nxt.mnemonic == "call"):
                        continue  # "mov eax, N" before a variadic call = number of vector args
                    out.append(Assign(self.operand(ci, dst), self.operand(ci, src), a))
                elif mn == "lea":
                    out.append(Assign(self.operand(ci, ops[0]), self.lea(ci, ops[1]), a))
                elif mn == "xor" and len(ops) == 2 and ops[0].type == ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                    out.append(Assign(self.operand(ci, ops[0]), Const(0), a))
                    flags = ("res", Const(0))
                elif mn in ARITH and len(ops) == 2:
                    dst = self.operand(ci, ops[0])
                    if dst == Var("r:rsp") or (self.bits == 32 and dst == Var("r:rsp", 4)):
                        continue  # stack adjustment
                    if isinstance(dst, Var) and dst.key == "r:rsp":
                        continue
                    src = self.operand(ci, ops[1])
                    out.append(Assign(dst, binop(ARITH[mn], dst, src), a))
                    flags = ("res", dst)  # compare the result with zero
                elif mn == "imul" and len(ops) == 3:
                    out.append(Assign(self.operand(ci, ops[0]), binop("*", self.operand(ci, ops[1]), self.operand(ci, ops[2])), a))
                elif mn in ("inc", "dec"):
                    dst = self.operand(ci, ops[0])
                    out.append(Assign(dst, binop("+" if mn == "inc" else "-", dst, Const(1)), a))
                    flags = ("res", dst)
                elif mn in ("neg", "not"):
                    dst = self.operand(ci, ops[0])
                    out.append(Assign(dst, Un("-" if mn == "neg" else "~", dst), a))
                    flags = ("res", dst)
                elif mn in ("cmp", "test"):
                    flags = (mn, self.operand(ci, ops[0]), self.operand(ci, ops[1]))
                elif mn.startswith("set") and len(ops) == 1:
                    out.append(Assign(self.operand(ci, ops[0]), cond_from_flags(flags, mn[3:]), a))
                elif mn.startswith("cmov") and len(ops) == 2:
                    dst = self.operand(ci, ops[0])
                    out.append(Assign(dst, Ternary(cond_from_flags(flags, mn[4:]), self.operand(ci, ops[1]), dst), a))
                elif mn in ("imul", "mul") and len(ops) == 1:
                    src = self.operand(ci, ops[0])
                    rax, rdx = Var("r:rax", ops[0].size), Var("r:rdx", ops[0].size)
                    out.append(Assign(rdx, Bin(">>", Bin("*", rax, src), Const(ops[0].size * 8)), a))
                    out.append(Assign(rax, Bin("*", rax, src), a))
                elif mn in ("idiv", "div") and len(ops) == 1:
                    src = self.operand(ci, ops[0])
                    rax, rdx = Var("r:rax"), Var("r:rdx")
                    out.append(Assign(Var("r:tmp"), rax, a))
                    out.append(Assign(rax, Bin("/", Var("r:tmp"), src), a))
                    out.append(Assign(rdx, Bin("%", Var("r:tmp"), src), a))
                elif mn == "call":
                    out.append(self.call(ci, ops, out))
                elif mn in ("ret", "retn"):
                    lb.term = Return(Var("r:" + self.ret), a)
                elif mn == "jmp":
                    tgt = ops[0].imm if ops and ops[0].type == x86.X86_OP_IMM else None
                    fn = self.ctx.function
                    slot_tgt = self.ctx.call_target_of(a)
                    if tgt is not None and tgt not in fn.tail_calls:
                        lb.term = Goto(tgt, a)
                    elif tgt is not None or slot_tgt is not None:
                        t = tgt if tgt is not None else slot_tgt
                        call = self.call_expr(AddrOf(t), t, out)
                        lb.term = Return(call, a)
                    else:
                        lb.term = Goto(None, a, self.operand(ci, ops[0]))
                elif mn.startswith("j") and ops and ops[0].type == x86.X86_OP_IMM:
                    cc = mn[1:]
                    if cc in ("ecxz", "rcxz", "cxz"):
                        cond = Bin("==", Var("r:rcx"), Const(0))
                    else:
                        cond = cond_from_flags(flags, cc)
                    lb.term = Branch(cond, ops[0].imm, a + ci.size, a)
                elif mn == "hlt":
                    out.append(ExprStmt(Call(Var("r:__halt"), ()), a))
                else:
                    out.append(Asm(f"{ci.mnemonic} {ci.op_str}".strip(), a))
            except Exception as exc:  # never let one odd instruction kill the function
                out.append(Asm(f"{ci.mnemonic} {ci.op_str}  /* lift error: {type(exc).__name__} */", a))
        return lb

    # -- calls ------------------------------------------------------------
    def call_expr(self, target: Expr, tgt_addr: Optional[int], out: list) -> Call:
        name = self.ctx.callee_name(tgt_addr) if tgt_addr is not None else None
        known = KNOWN_ARGS.get((name or "").lstrip("_")) if name else None
        if known is None and tgt_addr is not None:
            known = self.ctx.param_count(tgt_addr)
        if self.abi == "cdecl":
            pending = {}
            for st in reversed(out):
                if isinstance(st, (ExprStmt,)) or (isinstance(st, Assign) and isinstance(st.src, Call)):
                    break
                if isinstance(st, Assign) and isinstance(st.dst, Var) and st.dst.key.startswith("o:"):
                    off = int(st.dst.key[2:])
                    pending.setdefault(off, st.dst)
            n = 0
            while n * 4 in pending:
                n += 1
            if known is not None:
                n = known if known <= n or known <= 6 else n
            args = tuple(pending.get(i * 4, Var(f"o:{i * 4}", 4)) for i in range(n))
        else:
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
            while n < len(self.args) and self.args[n] in assigned:
                n += 1
            if known is not None:
                n = min(known, len(self.args)) if known > n or n > known else n
            args = tuple(Var("r:" + r) for r in self.args[:n])
        return Call(target, args)

    def call(self, ci, ops, out: list) -> Assign:
        tgt_addr = self.ctx.call_target_of(ci.address)
        if tgt_addr is not None:
            target: Expr = AddrOf(tgt_addr)
        elif ops[0].type == x86.X86_OP_IMM:
            tgt_addr = ops[0].imm
            target = AddrOf(tgt_addr)
        else:
            target = self.operand(ci, ops[0])
        return Assign(Var("r:" + self.ret), self.call_expr(target, tgt_addr, out), ci.address, CLOBBER[self.abi])
