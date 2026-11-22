"""Example scripts run both with a normal Python and inside the app (`reversa script`)."""
import os
import subprocess
import sys

import pytest
from conftest import EXAMPLES, EXE, env_for_child, sample

SCRIPTS = {
    "strcmp_callers.py": (["login_x64.elf"], ["check_password", "check_user"]),
    "function_table.py": (["hello_x64.elf"], ["sum_to", "classify", "greet"]),
    "annotate_and_view.py": (["login_x64_stripped.elf"], ["_compares_strings"]),
}


@pytest.mark.parametrize("runner", ["python", "app"])
@pytest.mark.parametrize("script", sorted(SCRIPTS))
def test_example_scripts(script, runner, tmp_path):
    args, expected = SCRIPTS[script]
    extra = [str(tmp_path / "out.rvproj")] if script == "annotate_and_view.py" else []
    cmd = [sys.executable] if runner == "python" else [EXE, "script"]
    r = subprocess.run(cmd + [os.path.join(EXAMPLES, "scripts", script)] + [sample(a) for a in args] + extra,
                       capture_output=True, text=True, encoding="utf-8", env=env_for_child(tmp_path), timeout=120)
    assert r.returncode == 0, r.stderr
    for e in expected:
        assert e in r.stdout
    if extra:
        assert (tmp_path / "out.rvproj").exists()


def test_strcmp_script_is_short():
    code = open(os.path.join(EXAMPLES, "scripts", "strcmp_callers.py"), encoding="utf-8").read()
    logic = [ln for ln in code.splitlines() if ln.strip() and not ln.startswith(('"""', "#")) and "Spustenie" not in ln and "Vypíše" not in ln]
    assert len(logic) <= 10


def test_script_errors_and_exit_codes(tmp_path):
    bad = tmp_path / "bad.py"
    bad.write_text("import sys\nprint('ahoj')\nraise ValueError('zámerná chyba')\n", encoding="utf-8")
    r = subprocess.run([EXE, "script", str(bad)], capture_output=True, text=True, encoding="utf-8", env=env_for_child(tmp_path))
    assert r.returncode == 1 and "ahoj" in r.stdout and "ValueError" in r.stderr
    ex = tmp_path / "exit.py"
    ex.write_text("import sys\nsys.exit(3)\n")
    assert subprocess.run([EXE, "script", str(ex)], env=env_for_child(tmp_path)).returncode == 3
