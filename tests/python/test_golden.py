"""Analysis facts must match samples/expected/<name>.json (golden files, shared with REVERSA 0.1)."""
import importlib.util
import json
import os

import pytest
from conftest import ALL_SAMPLES, ROOT


@pytest.mark.parametrize("name", ALL_SAMPLES)
def test_golden(name):
    spec = importlib.util.spec_from_file_location("make_expected", os.path.join(ROOT, "samples", "make_expected.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    with open(os.path.join(ROOT, "samples", "expected", name + ".json"), encoding="utf-8") as fh:
        expected = json.load(fh)
    assert mod.facts(os.path.join(ROOT, "samples", "bin", name)) == expected
