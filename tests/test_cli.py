import json
import os
import subprocess
import sys

from conftest import EXAMPLES, ROOT, sample

from reversa.cli import main


def test_analyze_report_markdown(tmp_path, capsys):
    out = tmp_path / "report.md"
    assert main(["analyze", sample("login_x64.elf"), "--report", str(out), "--no-plugins"]) == 0
    text = out.read_text()
    assert "# Analýza: login_x64.elf" in text and "check_password" in text and "```c" in text
    assert "Hlavná fn.:   main" in capsys.readouterr().out


def test_analyze_html_json_project(tmp_path, capsys):
    out = tmp_path / "r.html"
    proj = tmp_path / "p.rvproj"
    assert main(["analyze", sample("hello_arm64.elf"), "--report", str(out), "--json", "--project", str(proj),
                 "--pseudocode", "sum_to", "--no-plugins"]) == 0
    printed = capsys.readouterr().out
    info = json.loads(printed[:printed.index("\n}") + 2])
    assert info["arch"] == "arm64" and info["main"] == "main" and info["functions"] > 5
    assert "while (" in printed
    assert out.read_text().startswith("<!doctype html>") and proj.exists()


def test_raw_analysis_with_manual_arch(tmp_path, capsys):
    f = tmp_path / "fw.bin"
    f.write_bytes(bytes.fromhex("fd7bbfa9fd030091200080d2fd7bc1a8c0035fd6"))
    assert main(["analyze", str(f), "--arch", "arm64", "--base", "0x8000", "--pseudocode", "0x8000", "--no-plugins"]) == 0
    out = capsys.readouterr().out
    assert "RAW / arm64" in out and "return 1;" in out


def test_demo_terminal(capsys):
    assert main(["--demo", "--no-gui"]) == 0
    out = capsys.readouterr().out
    assert "check_password" in out and "check_user" in out and "Access granted" in out


def test_info_search_script(tmp_path, capsys):
    assert main(["info", sample("hello_x64.exe"), "--no-plugins"]) == 0
    assert ".text" in capsys.readouterr().out
    assert main(["search", sample("login_x64.elf"), "--string", "admin", "--no-plugins"]) == 0
    assert "admin" in capsys.readouterr().out
    assert main(["search", sample("login_x64.elf"), "--hex", "f3 0f 1e fa", "--no-plugins"]) == 0
    assert main(["search", sample("login_x64.elf"), "--insn", "call.*", "--no-plugins"]) == 0
    assert main(["search", sample("login_x64.elf"), "--code", r"strcmp\(", "--no-plugins"]) == 0
    capsys.readouterr()
    assert main(["script", os.path.join(EXAMPLES, "scripts", "strcmp_callers.py"), sample("login_x64.elf")]) == 0
    assert "check_password" in capsys.readouterr().out


def test_errors_are_friendly(tmp_path, capsys):
    assert main(["analyze", str(tmp_path / "missing.exe")]) == 1
    err = capsys.readouterr().err
    assert "Chyba: Súbor" in err and "Traceback" not in err
    bad = tmp_path / "bad.elf"
    bad.write_bytes(b"\x7fELF" + b"\x09" * 80)
    assert main(["analyze", str(bad), "--no-plugins"]) == 0
    assert "poškodený" in capsys.readouterr().out
    assert main(["analyze", str(bad), "--no-plugins", "--fail-on-error"]) == 1
    assert main([]) == 2


def test_console_entry_point_runs_as_module(tmp_path):
    env = dict(os.environ, PYTHONPATH=os.path.join(ROOT, "src"))
    r = subprocess.run([sys.executable, "-m", "reversa", "analyze", sample("login_x64.elf"), "--report",
                        str(tmp_path / "ci.md"), "--no-plugins"], capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stderr
    assert (tmp_path / "ci.md").exists()
