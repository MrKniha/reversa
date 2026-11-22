"""Python tests run against the C++ core (the `reversa` package from the build tree).

Build first (see README), then:  REVERSA_BUILD_DIR=build python -m pytest tests/python
"""
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _build_dir() -> str:
    env = os.environ.get("REVERSA_BUILD_DIR")
    if env:
        return os.path.abspath(env)
    for cand in ("build", "build-py"):
        d = os.path.join(ROOT, cand)
        if os.path.isdir(os.path.join(d, "bin", "python", "reversa")):
            return d
    raise RuntimeError("Nenašiel som zostavenie REVERSA (nastavte REVERSA_BUILD_DIR).")


BUILD = _build_dir()
BIN = os.path.join(BUILD, "bin")
PYDIR = os.path.join(BIN, "python")
EXE = os.path.join(BIN, "reversa.exe" if os.name == "nt" else "reversa")
sys.path.insert(0, PYDIR)
SAMPLES = os.path.join(ROOT, "samples", "bin")
EXAMPLES = os.path.join(ROOT, "examples")

ALL_SAMPLES = sorted(f for f in os.listdir(SAMPLES) if not f.endswith("SUMS"))


def sample(name: str) -> str:
    return os.path.join(SAMPLES, name)


_cache = {}


def program(name: str):
    """Analysed Program, cached per test session (treat as read-only!)."""
    import reversa
    if name not in _cache:
        _cache[name] = reversa.open(sample(name), load_plugins=False)
    return _cache[name]


def fresh(name: str, **kw):
    import reversa
    kw.setdefault("load_plugins", False)
    return reversa.open(sample(name), **kw)


def env_for_child(tmp_path) -> dict:
    return dict(os.environ, PYTHONPATH=PYDIR, HOME=str(tmp_path), USERPROFILE=str(tmp_path), PYTHONIOENCODING="utf-8")


@pytest.fixture(autouse=True)
def _isolated_home(tmp_path, monkeypatch):
    # plugins/projects must never touch the real home directory during tests
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
    monkeypatch.setenv("USERPROFILE", str(tmp_path / "home"))  # Windows expanduser
    monkeypatch.delenv("REVERSA_PLUGIN_PATH", raising=False)
