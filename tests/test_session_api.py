import pytest
from conftest import fresh, program, sample

import reversa
from reversa.core.errors import AnnotationError, ReversaError
from reversa.core.events import EventBus
from reversa.core.undo import Command, UndoStack


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
    with pytest.raises(ReversaError):
        p.function(0x10)


def test_event_bus_isolates_failing_listener():
    errors = []
    bus = EventBus(on_error=lambda topic, h, exc: errors.append(str(exc)))
    seen = []
    bus.subscribe("x", lambda **kw: 1 / 0)
    unsub = bus.subscribe("x", lambda **kw: seen.append(kw))
    bus.subscribe("*", lambda **kw: seen.append("star"))
    bus.publish("x", a=1)
    assert seen == [{"topic": "x", "a": 1}, "star"] and errors and bus.revision == 1
    unsub()
    bus.publish("x")
    assert len(seen) == 3


def test_undo_stack_limit_and_redo_cleared():
    st = UndoStack(limit=2)
    log = []
    for i in range(3):
        st.push(Command(str(i), lambda i=i: log.append(i), lambda i=i: log.append(-i)))
    assert st.undo() == "2" and st.undo() == "1" and st.undo() is None
    assert st.redo() == "1"
    st.push(Command("x", lambda: None, lambda: None))
    assert not st.can_redo
    st.clear()
    assert not st.can_undo


def test_listener_failure_becomes_diagnostic():
    p = fresh("login_x64.elf")
    p.session.events.subscribe("renamed", lambda **kw: 1 / 0)
    p.rename("main", "start_here")
    assert any("Poslucháč" in d.message for d in p.session.diagnostics)
