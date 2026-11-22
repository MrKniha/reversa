"""Mutation fuzzing of the format parsers + full analysis.

Deterministic (fixed seeds) so CI is reproducible. Raise REVERSA_FUZZ_ITERS for
longer local runs, e.g. REVERSA_FUZZ_ITERS=5000 pytest tests/test_fuzz.py
"""
import os
import random

import pytest
from conftest import sample

from reversa.analysis.analyzer import analyze
from reversa.formats.loader import load_bytes

ITERS = int(os.environ.get("REVERSA_FUZZ_ITERS", "60"))
SEEDS = ["login_x64.elf", "login_arm64.elf", "login_x64_stripped.exe", "login_x86.exe"]


def mutate(rng: random.Random, data: bytearray) -> bytes:
    d = bytearray(data)
    for _ in range(rng.randint(1, 12)):
        op = rng.random()
        hot = rng.randint(0, min(len(d) - 1, 0x400)) if rng.random() < 0.7 else rng.randint(0, len(d) - 1)
        if op < 0.6:
            d[hot] = rng.randint(0, 255)
        elif op < 0.8:
            for k in range(4):
                if hot + k < len(d):
                    d[hot + k] = rng.choice([0x00, 0xFF, 0x7F, 0x80])
        else:
            del d[rng.randint(1, len(d)):]  # truncate
            if not d:
                d = bytearray(b"\x7f")
    return bytes(d)


@pytest.mark.parametrize("seed_file", SEEDS)
def test_mutations_never_crash(seed_file):
    base = bytearray(open(sample(seed_file), "rb").read())
    rng = random.Random(seed_file)
    for i in range(ITERS):
        data = mutate(rng, base)
        b = load_bytes(data, f"fuzz{i}")
        a = analyze(b)
        assert a.done


@pytest.mark.parametrize("seed_file", ["hello_x64.elf", "hello_x64.exe"])
def test_truncation_at_every_boundary(seed_file):
    data = open(sample(seed_file), "rb").read()
    for cut in list(range(1, 0x200, 7)) + [len(data) // 3, len(data) // 2, len(data) - 1]:
        b = load_bytes(data[:cut], "trunc")
        analyze(b)


def test_random_garbage():
    rng = random.Random(1234)
    for n in (1, 3, 64, 4096):
        for prefix in (b"", b"\x7fELF", b"MZ"):
            blob = prefix + bytes(rng.randint(0, 255) for _ in range(n))
            analyze(load_bytes(blob, "garbage"))
