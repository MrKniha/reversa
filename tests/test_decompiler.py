import re

import pytest
from conftest import program

from reversa.decompiler import ir
from reversa.decompiler.ir import Bin, Const, Un, Var, add, binop, negate, substitute
from reversa.decompiler.lift_common import cond_from_flags

ARCHES = ["hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe"]


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
    from conftest import fresh
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


def test_ir_helpers():
    x = Var("r:rax")
    assert add(x, Const(0)) == x
    assert add(Const(0), x) == x
    assert add(Const(2), Const(3)) == Const(5)
    assert add(x, Const(-4)) == Bin("-", x, Const(4))
    assert add(Bin("+", x, Const(1)), Const(2)) == Bin("+", x, Const(3))
    assert binop("*", x, Const(1)) == x
    assert binop("&", Bin("==", x, Const(0)), Const(0xFF)) == Bin("==", x, Const(0))
    assert negate(Bin("<", x, Const(1))) == Bin(">=", x, Const(1))
    assert negate(Un("!", x)) == x
    assert negate(Bin("&&", Bin("==", x, Const(1)), Bin("<", x, Const(2)))).op == "||"
    assert substitute(Bin("+", x, x), "r:rax", Const(2)) == Const(4)
    assert list(ir.walk(ir.Call(x, (Const(1),))))[-1] == Const(1)


def test_conditions_from_flags():
    a, b = Var("s:-4"), Const(2)
    assert cond_from_flags(("cmp", a, b), "le") == Bin("<=", a, b)
    assert cond_from_flags(("cmp", a, b), "b") == Bin("u<", a, b)
    assert cond_from_flags(("test", a, a), "e") == Bin("==", a, Const(0))
    assert cond_from_flags(("test", a, b), "ne") == Bin("!=", Bin("&", a, b), Const(0))
    assert cond_from_flags(("res", a), "s") == Bin("<", a, Const(0))
    assert cond_from_flags(None, "e").op == "?"
    assert cond_from_flags(("cmp", a, b), "weird").op == "?"


def test_every_function_decompiles_without_crash():
    for name in ["hello_x64.exe", "login_arm64.elf", "hello_x86.exe"]:
        p = program(name)
        for f in p.functions():
            text = f.pseudocode()
            assert text.startswith("// Dočasný pseudokód"), (name, f.name)
            assert "Pseudokód sa nepodarilo" not in text, (name, f.name, text)
