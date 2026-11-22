"""Capstone (BSD-3-Clause) based disassembler backend.

Why Capstone: multi-arch, mature, permissive license, Python bindings with
operand details (needed for cross-references). Alternatives considered:
Zydis/iced (x86 only), binutils (GPL, text only). See docs/research.md.
"""
from __future__ import annotations

from typing import Iterator, Optional

import capstone
from capstone import arm64 as a64
from capstone import x86

from reversa.disasm.base import (FLOW_CALL, FLOW_CJMP, FLOW_ICALL, FLOW_IJMP, FLOW_JMP, FLOW_RET, FLOW_SEQ,
                                 FLOW_TRAP, Insn)

_MODES = {
    "x86": (capstone.CS_ARCH_X86, capstone.CS_MODE_32),
    "x86-64": (capstone.CS_ARCH_X86, capstone.CS_MODE_64),
    "arm64": (capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM),
    "arm": (capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM),
}
ARM64_CJMP = ("cbz", "cbnz", "tbz", "tbnz")


class RefTracker:
    """Per-basic-block constant tracking for ARM64 ``adrp``/``adr`` address building."""

    def __init__(self) -> None:
        self.regs: dict[int, int] = {}


class CapstoneDisassembler:
    name = "capstone"

    def __init__(self, arch: str, endian: str = "little", is_mapped=None):
        if arch not in _MODES:
            raise ValueError(f"Architektúra {arch} nie je podporovaná disassemblerom.")
        a, m = _MODES[arch]
        if endian == "big":
            m |= capstone.CS_MODE_BIG_ENDIAN
        self.arch = arch
        self.cs = capstone.Cs(a, m)
        self.cs.detail = True
        self.cs.skipdata = False
        self.is_mapped = is_mapped or (lambda addr: False)

    def raw(self, code: bytes, addr: int, count: int = 0):
        """Yield raw capstone instructions (used by the decompiler lifter)."""
        return self.cs.disasm(code, addr, count)

    def decode(self, code: bytes, addr: int, count: int = 0, tracker: Optional[RefTracker] = None) -> Iterator[Insn]:
        for ci in self.cs.disasm(code, addr, count):
            yield self.convert(ci, tracker)

    def decode_one(self, code: bytes, addr: int, tracker: Optional[RefTracker] = None) -> Optional[Insn]:
        for ins in self.decode(code, addr, 1, tracker):
            return ins
        return None

    # ------------------------------------------------------------------
    def convert(self, ci, tracker: Optional[RefTracker] = None) -> Insn:
        ins = Insn(ci.address, ci.size, ci.mnemonic, ci.op_str, bytes(ci.bytes))
        if self.arch in ("x86", "x86-64"):
            self._x86(ci, ins)
        elif self.arch == "arm64":
            self._arm64(ci, ins, tracker)
        else:
            self._generic(ci, ins)
        return ins

    def _x86(self, ci, ins: Insn) -> None:
        groups = set(ci.groups)
        ops = ci.operands
        imm = next((o.imm for o in ops if o.type == x86.X86_OP_IMM), None)
        if capstone.CS_GRP_CALL in groups:
            ins.flow = FLOW_CALL if imm is not None else FLOW_ICALL
            ins.target = imm
        elif capstone.CS_GRP_RET in groups or ci.mnemonic in ("ret", "retf", "iret", "iretq"):
            ins.flow = FLOW_RET
        elif capstone.CS_GRP_JUMP in groups:
            if ci.mnemonic in ("jmp", "bnd jmp", "notrack jmp"):
                ins.flow = FLOW_JMP if imm is not None else FLOW_IJMP
            else:
                ins.flow = FLOW_CJMP
            ins.target = imm
        elif ci.mnemonic in ("hlt", "ud2", "int3"):
            ins.flow = FLOW_TRAP
        for o in ops:
            if o.type == x86.X86_OP_MEM:
                m = o.mem
                addr = None
                if m.base in (x86.X86_REG_RIP, x86.X86_REG_EIP):
                    addr = ci.address + ci.size + m.disp
                elif m.base == 0 and m.segment == 0:
                    addr = m.disp & ((1 << 64) - 1 if self.arch == "x86-64" else 0xFFFFFFFF)
                    if m.index != 0 and not self.is_mapped(addr):
                        addr = None  # scaled index without a table base: not an address
                if addr is not None:
                    if ci.mnemonic == "lea":
                        ins.refs.append((addr, "addr"))
                    else:
                        acc = o.access
                        if acc & capstone.CS_AC_WRITE:
                            ins.refs.append((addr, "write"))
                        if acc & capstone.CS_AC_READ or not acc:
                            ins.refs.append((addr, "read"))
                    if ins.flow in (FLOW_ICALL, FLOW_IJMP):
                        ins.mem_slot = addr
            elif o.type == x86.X86_OP_IMM and ins.flow == FLOW_SEQ and o.imm >= 0x1000 and self.is_mapped(o.imm):
                ins.refs.append((o.imm, "addr"))

    def _arm64(self, ci, ins: Insn, tracker: Optional[RefTracker]) -> None:
        mn = ci.mnemonic
        ops = ci.operands
        imm = next((o.imm for o in ops if o.type == a64.ARM64_OP_IMM), None)
        if mn == "bl":
            ins.flow, ins.target = FLOW_CALL, imm
        elif mn == "blr":
            ins.flow = FLOW_ICALL
        elif mn == "b":
            ins.flow, ins.target = FLOW_JMP, imm
        elif mn.startswith("b.") or mn in ARM64_CJMP:
            ins.flow, ins.target = FLOW_CJMP, imm
        elif mn == "br":
            ins.flow = FLOW_IJMP
        elif mn in ("ret", "eret"):
            ins.flow = FLOW_RET
        elif mn in ("brk", "udf", "hlt"):
            ins.flow = FLOW_TRAP
        if tracker is None:
            return
        regs = tracker.regs
        if mn in ("adrp", "adr") and len(ops) == 2:
            regs[ops[0].reg] = ops[1].imm
            if mn == "adr":
                ins.refs.append((ops[1].imm, "addr"))
            return
        if mn == "add" and len(ops) == 3 and ops[1].type == a64.ARM64_OP_REG and ops[1].reg in regs \
                and ops[2].type == a64.ARM64_OP_IMM:
            val = regs[ops[1].reg] + ops[2].imm
            regs[ops[0].reg] = val
            ins.refs.append((val, "addr"))
            return
        for o in ops:
            if o.type == a64.ARM64_OP_MEM and o.mem.base in regs:
                addr = regs[o.mem.base] + o.mem.disp
                kind = "write" if mn.startswith("st") else "read"
                ins.refs.append((addr, kind))
                if mn.startswith("ld") and ops[0].type == a64.ARM64_OP_REG:
                    regs.pop(ops[0].reg, None)
                    tracker.regs[("slot", ops[0].reg)] = addr  # remembered for "br x17" PLT stubs
                return
        # any other write to a tracked register invalidates it
        if ops and ops[0].type == a64.ARM64_OP_REG and mn not in ("str", "stp", "cmp", "cmn", "tst") and not mn.startswith("b"):
            regs.pop(ops[0].reg, None)
        if mn in ("br", "blr") and ops:
            slot = tracker.regs.get(("slot", ops[0].reg))
            if slot is not None:
                ins.mem_slot = slot

    def _generic(self, ci, ins: Insn) -> None:
        groups = set(ci.groups)
        imm = None
        for o in ci.operands:
            if getattr(o, "type", None) == 2:  # IMM in arm.h
                imm = o.imm
        if capstone.CS_GRP_CALL in groups:
            ins.flow, ins.target = (FLOW_CALL if imm is not None else FLOW_ICALL), imm
        elif capstone.CS_GRP_RET in groups or (ci.mnemonic == "bx" and ci.op_str == "lr") or \
                (ci.mnemonic.startswith("pop") and "pc" in ci.op_str):
            ins.flow = FLOW_RET
        elif capstone.CS_GRP_JUMP in groups:
            ins.flow = FLOW_JMP if ci.mnemonic == "b" else FLOW_CJMP
            ins.target = imm
