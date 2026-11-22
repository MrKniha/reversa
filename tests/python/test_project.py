"""Project files (.rvproj, SQLite) written and read by the C++ core."""
import os
import sqlite3

import pytest
from conftest import fresh, sample

import reversa
from reversa import ProjectError, _core


def test_roundtrip_preserves_everything(tmp_path):
    p = fresh("login_x64_stripped.elf")
    f1 = p.callers_of("strcmp")[0]
    p.rename(f1.addr, "check_password")
    p.comment(f1.addr, "porovnáva heslo")
    p.bookmark(p.main.addr, "začiatok")
    p.rename_variable(p.main.addr, "s:-4", "argc")
    p.session.settings["theme"] = "dark"
    path = p.save(str(tmp_path / "login.rvproj"))
    assert _core.is_project(path)

    q = reversa.open(path, load_plugins=False)
    assert q.session.project_path == path
    assert q.name_of(f1.addr) == "check_password"
    assert q.get_comment(f1.addr) == "porovnáva heslo"
    assert q.session.annotations.bookmarks[q.main.addr] == "začiatok"
    assert "argc" in q.main.pseudocode()
    assert "check_password(" in q.main.pseudocode()
    assert q.session.settings["theme"] == "dark"
    assert [f.name for f in q.callers_of("strcmp")][0] == "check_password"
    assert q.save() == path  # saving again goes to the same file


def test_project_is_plain_sqlite(tmp_path):
    path = fresh("hello_x64.elf").save(str(tmp_path / "h.rvproj"))
    con = sqlite3.connect(path)
    tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    assert {"meta", "binary", "names", "comments", "bookmarks", "var_names"} <= tables
    assert con.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()[0] == "1"
    con.close()


def test_project_not_a_binary(tmp_path):
    assert not _core.is_project(sample("hello_x64.elf"))
    assert not _core.is_project(str(tmp_path / "missing.rvproj"))


def test_corrupted_project(tmp_path):
    bad = tmp_path / "bad.rvproj"
    bad.write_bytes(b"SQLite format 3\x00" + b"garbage" * 50)
    with pytest.raises(ProjectError):
        _core.load_project(str(bad))
    with pytest.raises(ProjectError):
        _core.load_project(str(tmp_path / "nope.rvproj"))


def test_newer_schema_is_rejected(tmp_path):
    path = fresh("hello_x64.elf").save(str(tmp_path / "h.rvproj"))
    con = sqlite3.connect(path)
    with con:
        con.execute("UPDATE meta SET value='99' WHERE key='schema_version'")
    con.close()
    with pytest.raises(ProjectError, match="newer REVERSA"):
        _core.Session.open_project(path)


def test_tampered_binary_detected(tmp_path):
    path = fresh("hello_x64.elf").save(str(tmp_path / "h.rvproj"))
    con = sqlite3.connect(path)
    with con:
        con.execute("UPDATE binary SET data = ?", (b"\x7fELF" + b"\0" * 100,))
    con.close()
    with pytest.raises(ProjectError, match="SHA-256"):
        _core.load_project(path)


def test_save_error_is_friendly(tmp_path):
    with pytest.raises(ProjectError):
        fresh("hello_x64.elf").save(str(tmp_path / "no" / "such" / "dir" / "x.rvproj"))


def test_default_project_path(tmp_path):
    target = tmp_path / "bins"
    target.mkdir()
    assert _core.default_project_path(str(target / "a.out")) == str(target / "a.out.rvproj")
    assert "reversa-projects" in _core.default_project_path("<demo>/login_x64.elf")


def test_reads_projects_written_by_reversa_0_1(tmp_path):
    """A project in the 0.1 (Python) layout: load_options as JSON with arch/base/raw."""
    data = open(sample("hello_x64.elf"), "rb").read()
    import hashlib
    import json
    path = str(tmp_path / "old.rvproj")
    con = sqlite3.connect(path)
    with con:
        con.executescript("""
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE binary (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, path TEXT, sha256 TEXT, data BLOB, load_options TEXT);
CREATE TABLE names (addr INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE comments (addr INTEGER PRIMARY KEY, text TEXT NOT NULL);
CREATE TABLE bookmarks (addr INTEGER PRIMARY KEY, label TEXT NOT NULL);
CREATE TABLE var_names (func INTEGER, key TEXT, name TEXT NOT NULL, PRIMARY KEY (func, key));""")
        con.executemany("INSERT INTO meta VALUES (?, ?)", [("schema_version", "1"), ("app", "REVERSA"), ("settings", "{}")])
        con.execute("INSERT INTO binary VALUES (1, ?, ?, ?, ?, ?)", ("hello_x64.elf", "/old/hello_x64.elf",
                    hashlib.sha256(data).hexdigest(), data, json.dumps({"arch": None, "base": None, "raw": False})))
        con.execute("INSERT INTO names VALUES (?, ?)", (0x401196, "my_func"))
    con.close()
    p = reversa.open(path, load_plugins=False)
    assert p.name_of(0x401196) == "my_func" and p.path == "/old/hello_x64.elf"
