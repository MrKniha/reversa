import json
import os

import pytest
from conftest import ALL_SAMPLES, ROOT, program

from reversa.analysis.graphs import build_cfg, call_graph
from reversa.analysis.search import parse_hex_pattern, search_text
from reversa.core.errors import ReversaError


@pytest.mark.parametrize("name", ALL_SAMPLES)
def test_golden(name):
    """Results must match samples/expected/<name>.json (golden files)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("make_expected", os.path.join(ROOT, "samples", "make_expected.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    facts = mod.facts
    with open(os.path.join(ROOT, "samples", "expected", name + ".json")) as fh:
        expected = json.load(fh)
    assert facts(os.path.join(ROOT, "samples", "bin", name)) == expected


@pytest.mark.parametrize("name", ["hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe"])
def test_main_and_callees(name):
    p = program(name)
    main = p.main
    assert main is not None and main.name.lstrip("_") == "main"
    callees = {c.name.lstrip("_") for c in main.callees}
    assert {"greet", "sum_to", "classify", "puts"} <= callees


def test_string_xrefs_point_into_functions():
    p = program("login_x64.elf")
    s = next(x for x in p.strings() if x.text == "admin")
    xs = p.xrefs_to(s.addr)
    assert xs and xs[0].function == "check_user" and xs[0].kind in ("addr", "read")


def test_data_pointer_xrefs():
    p = program("hello_x64.elf")
    s = next(x for x in p.strings() if x.text == "Ahoj")
    kinds = {x.kind for x in p.xrefs_to(s.addr)}
    assert "data" in kinds  # greetings[] table points to the string


def test_read_write_xrefs_on_global():
    p = program("hello_x64.elf")
    table = p.addr("greetings")
    readers = {x.function for x in p.xrefs_to(table) if x.kind == "read"}
    assert readers == {"greet"}


def test_thunk_resolution_pe():
    p = program("login_x64.exe")
    a = p.session.analysis
    thunk = next(addr for addr, tgt in a.thunk_target.items() if p.name_of(addr) == "strcmp")
    assert a.functions[thunk].kind == "thunk"
    assert {f.name for f in p.callers_of("strcmp")} == {"check_password", "check_user"}


def test_noreturn_stops_block():
    p = program("login_x64.elf")
    cp = p.function("check_password")
    blocks = cp._fn.blocks
    chk = p.addr("__stack_chk_fail")
    for blk in blocks.values():
        last = p.session.analysis.insns[blk.insns[-1]]
        if last.target == chk:
            assert blk.succs == []


def test_cfg_loop_detection():
    p = program("hello_x64.elf")
    g = build_cfg(p.function("sum_to")._fn)
    assert len(g["loops"]) == 1
    assert any(e["back"] for e in g["edges"])
    g2 = build_cfg(p.function("classify")._fn)
    assert not g2["loops"] and len(g2["nodes"]) >= 4


def test_callgraph_neighbourhood():
    p = program("hello_x64.elf")
    a = p.session.analysis
    full = call_graph(a.functions)
    sub = call_graph(a.functions, p.addr("sum_to"), 1)
    assert set(sub["nodes"]) == {p.addr("sum_to"), p.addr("main")}
    assert len(full["nodes"]) > len(sub["nodes"])


def test_search():
    p = program("login_x64.elf")
    assert p.search_bytes("f3 0f 1e fa")  # endbr64
    assert p.search_bytes("f30f1efa") == p.search_bytes("f3 0f 1e fa")
    assert p.search_bytes("f3 ?? 1e fa")
    assert [t for _, t in p.search_strings("ACCESS")]
    assert p.search_strings(r"^Usage", regex=True)
    hits = p.search_instructions(r"call\s+0x[0-9a-f]+")
    assert hits
    assert any("strcmp" in line for _, line in p.search_pseudocode(r"strcmp\("))
    with pytest.raises(ReversaError):
        parse_hex_pattern("zz")
    with pytest.raises(ReversaError):
        parse_hex_pattern("")
    with pytest.raises(ReversaError):
        search_text([(1, "a")], "(", regex=True)


def test_function_containing_and_sizes():
    p = program("hello_x64.elf")
    a = p.session.analysis
    st = p.function("sum_to")
    assert a.function_containing(st.addr + 5).addr == st.addr
    assert st.size > 10 and st._fn.insn_count > 5
    assert p.function(st.addr + 5) == st


def test_raw_shellcode_analysis():
    import reversa
    from reversa.formats.loader import load_bytes  # noqa: F401
    import tempfile
    code = bytes.fromhex("554889e5b8010000005dc3")
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as fh:
        fh.write(code)
    p = reversa.open(fh.name, arch="x86-64", base=0x1000, load_plugins=False)
    assert p.format == "RAW" and p.entry == 0x1000
    assert [f.addr for f in p.functions()] == [0x1000]
    assert "return 1;" in p.function(0x1000).pseudocode()
    os.unlink(fh.name)
