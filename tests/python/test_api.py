"""Scripting API (reversa.Program) on top of the C++ core."""
import re

import pytest
from conftest import fresh, program, sample

import reversa
from reversa import AnnotationError, ReversaError

ARCHES = ["hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe"]


def test_acceptance_script_strcmp_callers():
    prog = reversa.open(sample("login_x64.elf"))
    names = [f.name for f in prog.callers_of("strcmp")]
    assert names == ["check_password", "check_user"]


def test_rename_propagates_everywhere_and_undo_redo():
    p = fresh("login_x64.elf")
    events = []
    p.session.events.subscribe("renamed", lambda **kw: events.append(kw))
    p.rename("check_user", "verify_user")
    assert events and events[0]["new"] == "verify_user" and events[0]["old"] == "check_user"
    assert "verify_user" in [f.name for f in p.callers_of("strcmp")]          # x-refs / callers
    assert "verify_user(" in p.main.pseudocode()                              # pseudocode
    assert p.function("verify_user").addr == p.addr("verify_user")            # name lookup
    assert any(x.function == "verify_user" for x in p.xrefs_to("strcmp"))      # xref view
    assert p.undo().startswith("Premenovanie")
    assert "check_user(" in p.main.pseudocode()
    assert p.redo()
    assert "verify_user(" in p.main.pseudocode()
    p.rename("verify_user", "")  # empty -> back to original
    assert p.name_of(p.addr("check_user")) == "check_user"


def test_rename_validation():
    p = fresh("login_x64.elf")
    with pytest.raises(AnnotationError, match="nesmie začínať"):
        p.rename("main", "1abc")
    with pytest.raises(AnnotationError, match="už používa"):
        p.rename("main", "check_user")
    with pytest.raises(AnnotationError):
        p.rename("no_such_function", "x")
    with pytest.raises(ReversaError):
        p.addr("nothing_here")


def test_comments_bookmarks_undo():
    p = fresh("login_x64.elf")
    main = p.main.addr
    p.comment(main, "vstup programu")
    p.bookmark(main, "štart")
    assert p.get_comment(main) == "vstup programu"
    assert p.session.annotations.bookmarks[main] == "štart"
    p.undo()
    assert main not in p.session.annotations.bookmarks
    p.undo()
    assert p.get_comment(main) is None
    assert p.undo() is None  # nothing left


def test_resolve_forms():
    p = program("login_x86.exe")
    s = p.session
    assert {f.name for f in p.callers_of("strcmp")} == {f.name for f in p.callers_of("_strcmp")} == {"_check_password", "_check_user"}
    assert s.resolve(hex(p.main.addr)) == p.main.addr
    assert s.resolve(p.main.addr) == p.main.addr
    assert s.resolve("zzz_not_there") is None


def test_default_names_resolve_and_unmapped_rename_rejected():
    p = fresh("login_x64_stripped.elf")
    assert p.function("sub_401196").addr == 0x401196  # regression: default names were not resolvable
    with pytest.raises(AnnotationError):
        p.rename(0xdeadbeef00, "nowhere")


def test_api_misc():
    p = program("hello_x64.elf")
    assert p.arch == "x86-64" and p.format == "ELF" and repr(p).startswith("<Program")
    assert p.read("main", 4) == p.binary.read(p.main.addr, 4)
    assert p.read_string(next(s.addr for s in p.strings() if s.text == "Done.")) == "Done."
    ins = p.disassemble("main", 3)
    assert len(ins) == 3 and ins[0].addr == p.main.addr
    assert any(i.name == "printf" for i in p.imports())
    assert p.sections() and isinstance(p.exports(), list)
    assert len(p.functions(include_imports=True)) > len(p.functions())
    f = p.function("greet")
    assert repr(f).startswith("<Function greet") and not f.is_import and f.kind == "normal"
    assert p.function("printf").is_import
    assert {c.name for c in f.callers} == {"main"}
    assert hash(f) == hash(p.function("greet"))
    assert f.xrefs and p.decompile("greet") == f.pseudocode()
    assert [i.addr for i in f.instructions()][0] == f.addr
    with pytest.raises(ReversaError):
        p.function(0x10)


def test_listener_failure_becomes_diagnostic():
    p = fresh("login_x64.elf")
    p.session.events.subscribe("renamed", lambda **kw: 1 / 0)
    p.rename("main", "start_here")
    assert any("Poslucháč" in d.message for d in p.session.diagnostics)


def test_unsubscribe():
    p = fresh("login_x64.elf")
    seen = []
    unsub = p.session.events.subscribe("*", lambda **kw: seen.append(kw["topic"]))
    p.comment("main", "x")
    unsub()
    p.comment("main", "y")
    assert seen == ["comment"]


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
    assert "data" in {x.kind for x in p.xrefs_to(s.addr)}  # greetings[] table points to the string


def test_read_write_xrefs_on_global():
    p = program("hello_x64.elf")
    table = p.addr("greetings")
    assert {x.function for x in p.xrefs_to(table) if x.kind == "read"} == {"greet"}


def test_search():
    p = program("login_x64.elf")
    assert p.search_bytes("f3 0f 1e fa")  # endbr64
    assert p.search_bytes("f30f1efa") == p.search_bytes("f3 0f 1e fa")
    assert p.search_bytes("f3 ?? 1e fa")
    assert [t for _, t in p.search_strings("ACCESS")]
    assert p.search_strings(r"^Usage", regex=True)
    assert p.search_instructions(r"call\s+0x[0-9a-f]+")
    assert any("strcmp" in line for _, line in p.search_pseudocode(r"strcmp\("))
    for bad in ("zz", ""):
        with pytest.raises(ReversaError):
            p.search_bytes(bad)
    with pytest.raises(ReversaError):
        p.search_strings("(", regex=True)


def test_raw_shellcode_analysis(tmp_path):
    f = tmp_path / "sc.bin"
    f.write_bytes(bytes.fromhex("554889e5b8010000005dc3"))
    p = reversa.open(str(f), arch="x86-64", base=0x1000, load_plugins=False)
    assert p.format == "RAW" and p.entry == 0x1000
    assert [f.addr for f in p.functions()] == [0x1000]
    assert "return 1;" in p.function(0x1000).pseudocode()


# ---------------------------------------------------------------- pseudocode
def code(name, func):
    p = program(name)
    f = p.function(func) if p.session.resolve(func) else p.function("_" + func)
    return f.pseudocode()


@pytest.mark.parametrize("name", ARCHES)
def test_sum_to_is_a_while_loop(name):
    c = code(name, "sum_to")
    assert re.search(r"while \(\w+ <= \w+\)", c), c
    assert "+= " in c and "++;" in c
    assert "return " in c and "goto" not in c


@pytest.mark.parametrize("name", ARCHES)
def test_classify_if_else(name):
    c = code(name, "classify")
    assert c.count("if (") >= 2 and "else" in c and "goto" not in c


@pytest.mark.parametrize("name", ARCHES)
def test_main_calls_with_arguments(name):
    c = code(name, "main")
    assert re.search(r'puts\("Done\."\)', c), c
    assert re.search(r'printf\("sum_to\(%d\) = %d\\n", \w+, _?sum_to\(\w+\)\)', c), c
    assert "return 0;" in c


@pytest.mark.parametrize("name", ["login_x64.elf", "login_arm64.elf", "login_x64.exe", "login_x86.exe", "login_x64_stripped.elf"])
def test_login_main_structure(name):
    p = program(name)
    main = p.main or p.function("_main")
    c = main.pseudocode()
    assert c.count("if (") >= 3
    assert '"Access granted. Flag: CTF{xrefs_are_your_friend}"' in c
    assert "goto" not in c


def test_void_function_and_params():
    c = code("hello_x64.elf", "greet")
    assert c.splitlines()[1].startswith("void greet(int32_t a1)")
    assert "greetings[" in c  # array access on a global table


def test_string_compare_in_check_user():
    c = code("login_x64.elf", "check_user")
    assert 'strcmp(' in c and '"admin"' in c and '"student"' in c


def test_variable_rename_propagates_and_undoes():
    p = fresh("hello_x64.elf")
    f = p.function("sum_to")
    assert "local_4" in f.pseudocode()
    p.rename_variable("sum_to", "s:-4", "i")
    c = f.pseudocode()
    assert re.search(r"\bi\+\+;", c) and "local_4" not in c
    p.undo()
    assert "local_4" in f.pseudocode()


def test_import_pseudocode_message():
    p = program("login_x64.elf")
    assert "importovaná funkcia" in p.function("strcmp").pseudocode()


def test_every_function_decompiles_without_crash():
    for name in ["hello_x64.exe", "login_arm64.elf", "hello_x86.exe"]:
        p = program(name)
        for f in p.functions():
            text = f.pseudocode()
            assert text.startswith("// Dočasný pseudokód"), (name, f.name)
            assert "Pseudokód sa nepodarilo" not in text, (name, f.name, text)


def test_segments_for_gui():
    p = program("login_x64.elf")
    lines = p.session.decompile(p.main.addr)
    kinds = {seg[1] for ln in lines for seg in ln["segs"]}
    assert {"type", "func", "var", "str", "kw", "op"} <= kinds
    calls = [seg for ln in lines for seg in ln["segs"] if seg[1] == "func" and seg[0] == "check_user"]
    assert calls and calls[0][2] == p.addr("check_user")
