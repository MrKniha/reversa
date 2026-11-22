from reversa.disasm.capstone_backend import CapstoneDisassembler, RefTracker


def test_x86_64_flow_and_refs():
    d = CapstoneDisassembler("x86-64", is_mapped=lambda a: 0x400000 <= a < 0x500000)
    code = bytes.fromhex(
        "e800000000"      # call +0
        "ff1500100000"    # call [rip+0x1000]
        "488d0500100000"  # lea rax, [rip+0x1000]
        "7405"            # je
        "eb00"            # jmp
        "c3"              # ret
        "bf00104000"      # mov edi, 0x401000 (immediate pointing into mapped memory)
        "f4")             # hlt
    ins = list(d.decode(code, 0x401000))
    assert [i.flow for i in ins] == ["call", "icall", "seq", "cjmp", "jmp", "ret", "seq", "trap"]
    assert ins[0].target == 0x401005
    assert ins[1].mem_slot == 0x401005 + 6 + 0x1000
    assert (0x40100B + 7 + 0x1000, "addr") in ins[2].refs
    assert ins[3].target == ins[3].end + 5
    assert (0x401000, "addr") in ins[6].refs


def test_x86_memory_access_kinds():
    d = CapstoneDisassembler("x86-64")
    w = d.decode_one(bytes.fromhex("890500100000"), 0x1000)  # mov [rip+0x1000], eax
    r = d.decode_one(bytes.fromhex("8b0500100000"), 0x1000)  # mov eax, [rip+0x1000]
    assert w.refs == [(0x2006, "write")]
    assert r.refs == [(0x2006, "read")]


def test_x86_32_absolute():
    d = CapstoneDisassembler("x86")
    i = d.decode_one(bytes.fromhex("a100104000"), 0x401000)  # mov eax, [0x401000]
    assert (0x401000, "read") in i.refs


def test_arm64_adrp_tracking_and_flow():
    d = CapstoneDisassembler("arm64")
    t = RefTracker()
    # adrp x16, 0x1f000 ; ldr x17, [x16, #0xf98] ; add x16, x16, #0xf98 ; br x17
    code = bytes.fromhex("f00000b0" "11ce47f9" "10623e91" "20021fd6")
    ins = list(d.decode(code, 0x650, 0, t))
    assert ins[-1].flow == "ijmp"
    assert ins[1].refs and ins[1].refs[0][1] == "read"
    assert ins[-1].mem_slot == ins[1].refs[0][0]
    bl = d.decode_one(bytes.fromhex("05000094"), 0x1000)  # bl +0x14
    assert bl.flow == "call" and bl.target == 0x1014
    ret = d.decode_one(bytes.fromhex("c0035fd6"), 0)
    assert ret.flow == "ret"
    cb = d.decode_one(bytes.fromhex("400000b4"), 0x100)  # cbz x0, +8
    assert cb.flow == "cjmp" and cb.target == 0x108


def test_arm32_generic():
    d = CapstoneDisassembler("arm")
    bl = d.decode_one(bytes.fromhex("010000eb"), 0x8000)  # bl
    assert bl.flow == "call"
    bx = d.decode_one(bytes.fromhex("1eff2fe1"), 0x8000)  # bx lr
    assert bx.flow == "ret"


def test_unknown_arch():
    import pytest
    with pytest.raises(ValueError):
        CapstoneDisassembler("mips")
