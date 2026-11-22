#!/usr/bin/env python3
"""Headless end-to-end check used by CI on Linux and Windows: analyze every sample,
write reports and projects, run the demo and the strcmp example script.

Usage: python scripts/headless_check.py [output-dir]
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "headless-out")
CLI = [sys.executable, "-m", "reversa"]


def run(*args: str) -> str:
    res = subprocess.run(CLI + list(args), cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if res.returncode != 0:
        sys.exit(f"FAILED: reversa {' '.join(args)}\n{res.stdout}\n{res.stderr}")
    return res.stdout


os.makedirs(OUT, exist_ok=True)
samples = sorted(glob.glob(os.path.join(ROOT, "samples", "bin", "*.elf")) + glob.glob(os.path.join(ROOT, "samples", "bin", "*.exe")))
assert samples, "no samples found"
for f in samples:
    name = os.path.basename(f)
    run("analyze", f, "--report", os.path.join(OUT, name + ".md"), "--project", os.path.join(OUT, name + ".rvproj"),
        "--no-plugins", "--fail-on-error")
run("analyze", os.path.join(ROOT, "samples", "bin", "login_x64.elf"), "--report", os.path.join(OUT, "login.html"), "--no-plugins")
run("--demo", "--no-gui")
callers = run("script", os.path.join(ROOT, "examples", "scripts", "strcmp_callers.py"),
              os.path.join(ROOT, "samples", "bin", "login_x64.exe"))
assert "check_password" in callers and "check_user" in callers, callers
print(f"headless check OK: {len(samples)} samples, output in {OUT}")
