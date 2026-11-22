"""Backend-independent instruction record."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

FLOW_SEQ, FLOW_CALL, FLOW_ICALL, FLOW_JMP, FLOW_IJMP, FLOW_CJMP, FLOW_RET, FLOW_TRAP = (
    "seq", "call", "icall", "jmp", "ijmp", "cjmp", "ret", "trap")


@dataclass(slots=True)
class Insn:
    addr: int
    size: int
    mnemonic: str
    op_str: str
    raw: bytes
    flow: str = FLOW_SEQ
    target: Optional[int] = None  # direct branch / call target
    refs: list = field(default_factory=list)  # [(address, "read"|"write"|"addr")]
    mem_slot: Optional[int] = None  # memory operand address of an indirect call/jump (IAT/GOT slot)

    @property
    def end(self) -> int:
        return self.addr + self.size

    @property
    def text(self) -> str:
        return f"{self.mnemonic} {self.op_str}".strip()
