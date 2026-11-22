import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "src"))
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


@pytest.fixture(autouse=True)
def _isolated_home(tmp_path, monkeypatch):
    # plugins/projects must never touch the real home directory during tests
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
    monkeypatch.delenv("REVERSA_PLUGIN_PATH", raising=False)
