#!/usr/bin/env python3
"""Builds the portable Windows package (zip) from an installed REVERSA tree.

Usage (CI, after `cmake --install build --prefix dist/reversa`):
    python scripts/package_windows.py dist/reversa reversa-windows-x64.zip

Steps:
1. windeployqt copies the Qt DLLs and plugins next to reversa-gui.exe (dynamic linking, LGPL).
2. The Python runtime of *this* interpreter (the one REVERSA was built against) is bundled for
   scripts and plugins: python3XY.dll, the extension modules, and the standard library zipped into
   python3XY.zip, with a python3XY._pth file so the bundle never reads a system Python.
3. Smoke test of the bundled executables, then the zip.
"""
import os
import shutil
import subprocess
import sys
import zipfile

SKIP_LIB = {"test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "lib2to3", "site-packages", "venv",
            "__pycache__", "pydoc_data"}


def run(*cmd: str) -> str:
    print("+", " ".join(cmd), flush=True)
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        sys.exit(f"FAILED ({res.returncode}): {' '.join(cmd)}\n{res.stdout}\n{res.stderr}")
    return res.stdout


def bundle_python(bin_dir: str) -> None:
    base = sys.base_prefix
    tag = f"python{sys.version_info.major}{sys.version_info.minor}"
    for name in (f"{tag}.dll", "python3.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
        src = os.path.join(base, name)
        if os.path.exists(src):
            shutil.copy2(src, bin_dir)
    dlls = os.path.join(base, "DLLs")
    for name in os.listdir(dlls):
        if name.endswith((".pyd", ".dll")) and not name.startswith(("_tkinter", "tcl", "tk")):
            shutil.copy2(os.path.join(dlls, name), bin_dir)
    lib = os.path.join(base, "Lib")
    with zipfile.ZipFile(os.path.join(bin_dir, f"{tag}.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(lib):
            dirs[:] = [d for d in dirs if d not in SKIP_LIB]
            for f in files:
                if f.endswith(".py"):
                    full = os.path.join(root, f)
                    z.write(full, os.path.relpath(full, lib))
    with open(os.path.join(bin_dir, f"{tag}._pth"), "w", encoding="ascii") as fh:
        fh.write(f"{tag}.zip\n.\n")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    bin_dir = os.path.join(root, "bin")
    run("windeployqt", "--release", "--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw",
        os.path.join(bin_dir, "reversa-gui.exe"))
    bundle_python(bin_dir)

    # Smoke test with a clean environment: only the bundle may provide Qt and Python.
    exe = os.path.join(bin_dir, "reversa.exe")
    env_path = os.environ.get("SystemRoot", r"C:\Windows")
    os.environ["PATH"] = os.pathsep.join([os.path.join(env_path, "System32"), env_path])
    for var in ("PYTHONHOME", "PYTHONPATH"):
        os.environ.pop(var, None)
    print(run(exe, "--version"))
    sample = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "samples", "bin", "login_x64.exe")
    script = os.path.join(bin_dir, "examples", "scripts", "strcmp_callers.py")
    callers = run(exe, "script", script, sample)
    assert "check_password" in callers and "check_user" in callers, callers
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    print(run(exe, "--demo", "--no-gui"))

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for r, _, files in os.walk(root):
            for f in files:
                if "__pycache__" in r:
                    continue
                full = os.path.join(r, f)
                z.write(full, os.path.join("reversa", os.path.relpath(full, root)))
    print(f"package: {out} ({os.path.getsize(out) // 1024} KiB)")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
