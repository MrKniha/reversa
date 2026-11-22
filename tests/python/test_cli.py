"""The `reversa` command line program (C++)."""
import json
import os
import subprocess

from conftest import EXAMPLES, EXE, env_for_child, sample


def run(tmp_path, *args):
    r = subprocess.run([EXE, *args], capture_output=True, text=True, encoding="utf-8", env=env_for_child(tmp_path), timeout=120)
    return r.returncode, r.stdout, r.stderr


def test_analyze_report_markdown(tmp_path):
    out = tmp_path / "report.md"
    code, stdout, _ = run(tmp_path, "analyze", sample("login_x64.elf"), "--report", str(out), "--no-plugins")
    assert code == 0
    text = out.read_text(encoding="utf-8")
    assert "# Analýza: login_x64.elf" in text and "check_password" in text and "```c" in text
    assert "Hlavná fn.:   main" in stdout


def test_analyze_html_json_project(tmp_path):
    out = tmp_path / "r.html"
    proj = tmp_path / "p.rvproj"
    code, printed, _ = run(tmp_path, "analyze", sample("hello_arm64.elf"), "--report", str(out), "--json", "--project",
                           str(proj), "--pseudocode", "sum_to", "--no-plugins")
    assert code == 0
    info = json.loads(printed[:printed.index("\n}") + 2])
    assert info["arch"] == "arm64" and info["main"] == "main" and info["functions"] > 5
    assert "while (" in printed
    assert out.read_text(encoding="utf-8").startswith("<!doctype html>") and proj.exists()


def test_raw_analysis_with_manual_arch(tmp_path):
    f = tmp_path / "fw.bin"
    f.write_bytes(bytes.fromhex("fd7bbfa9fd030091200080d2fd7bc1a8c0035fd6"))
    code, out, _ = run(tmp_path, "analyze", str(f), "--arch", "arm64", "--base", "0x8000", "--pseudocode", "0x8000", "--no-plugins")
    assert code == 0 and "RAW / arm64" in out and "return 1;" in out


def test_demo_terminal(tmp_path):
    code, out, _ = run(tmp_path, "--demo", "--no-gui")
    assert code == 0 and "check_password" in out and "check_user" in out and "Access granted" in out


def test_info_search_script(tmp_path):
    assert ".text" in run(tmp_path, "info", sample("hello_x64.exe"), "--no-plugins")[1]
    assert "admin" in run(tmp_path, "search", sample("login_x64.elf"), "--string", "admin", "--no-plugins")[1]
    for args in (["--hex", "f3 0f 1e fa"], ["--insn", "call.*"], ["--code", r"strcmp\("]):
        code, out, err = run(tmp_path, "search", sample("login_x64.elf"), *args, "--no-plugins")
        assert code == 0 and out and "výsledkov" in err
    code, out, _ = run(tmp_path, "script", os.path.join(EXAMPLES, "scripts", "strcmp_callers.py"), sample("login_x64.elf"))
    assert code == 0 and "check_password" in out


def test_plugins_from_command_line(tmp_path):
    report = tmp_path / "r.md"
    code, out, _ = run(tmp_path, "analyze", sample("login_x64.elf"), "--plugins", os.path.join(EXAMPLES, "plugins"),
                       "--report", str(report))
    assert code == 0
    assert "[hello-plugin] načítaný súbor" in out          # on_load hook
    assert "crashing-plugin" in out and "pokračuje" in out  # crash isolated, reported
    text = report.read_text(encoding="utf-8")
    assert "## Pohľad: Hello plugin" in text and "Analyzovaných funkcií: 11" in text


def test_errors_are_friendly(tmp_path):
    code, _, err = run(tmp_path, "analyze", str(tmp_path / "missing.exe"))
    assert code == 1 and "Chyba: Súbor" in err
    bad = tmp_path / "bad.elf"
    bad.write_bytes(b"\x7fELF" + b"\x09" * 80)
    code, out, _ = run(tmp_path, "analyze", str(bad), "--no-plugins")
    assert code == 0 and "poškodený" in out
    assert run(tmp_path, "analyze", str(bad), "--no-plugins", "--fail-on-error")[0] == 1
    assert run(tmp_path)[0] == 2
    code, _, err = run(tmp_path, "analyze")
    assert code == 2 and "vyžaduje súbor" in err
    assert run(tmp_path, "--version")[1].startswith("REVERSA ")
