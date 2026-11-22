import os
import subprocess
import sys

import pytest
from conftest import EXAMPLES, ROOT, sample

SCRIPTS = {
    "strcmp_callers.py": (["login_x64.elf"], ["check_password", "check_user"]),
    "function_table.py": (["hello_x64.elf"], ["sum_to", "classify", "greet"]),
    "annotate_and_view.py": (["login_x64_stripped.elf"], ["_compares_strings"]),
}


@pytest.mark.parametrize("script", sorted(SCRIPTS))
def test_example_scripts(script, tmp_path):
    args, expected = SCRIPTS[script]
    env = dict(os.environ, PYTHONPATH=os.path.join(ROOT, "src"), HOME=str(tmp_path))
    extra = [str(tmp_path / "out.rvproj")] if script == "annotate_and_view.py" else []
    r = subprocess.run([sys.executable, os.path.join(EXAMPLES, "scripts", script)] + [sample(a) for a in args] + extra,
                       capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stderr
    for e in expected:
        assert e in r.stdout
    if extra:
        assert (tmp_path / "out.rvproj").exists()


def test_strcmp_script_is_short():
    code = open(os.path.join(EXAMPLES, "scripts", "strcmp_callers.py")).read()
    logic = [ln for ln in code.splitlines() if ln.strip() and not ln.startswith(('"""', "#")) and "Spustenie" not in ln and "Vypíše" not in ln]
    assert len(logic) <= 10
