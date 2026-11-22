import sqlite3

import pytest
from conftest import fresh, sample

import reversa
from reversa.core.errors import ProjectError
from reversa.project import store
from reversa.session import Session, default_project_path


def test_roundtrip_preserves_everything(tmp_path):
    p = fresh("login_x64_stripped.elf")
    f1 = p.callers_of("strcmp")[0]
    p.rename(f1.addr, "check_password")
    p.comment(f1.addr, "porovnáva heslo")
    p.bookmark(p.main.addr, "začiatok")
    p.rename_variable(p.main.addr, "s:-4", "argc")
    p.session.settings["theme"] = "dark"
    path = p.save(str(tmp_path / "login.rvproj"))
    assert store.is_project(path)

    q = reversa.open(path, load_plugins=False)
    assert q.session.project_path == path
    assert q.name_of(f1.addr) == "check_password"
    assert q.get_comment(f1.addr) == "porovnáva heslo"
    assert q.session.annotations.bookmarks[q.main.addr] == "začiatok"
    assert "argc" in q.main.pseudocode()
    assert "check_password(" in q.main.pseudocode()
    assert q.session.settings["theme"] == "dark"
    assert [f.name for f in q.callers_of("strcmp")][0] == "check_password"
    # saving again goes to the same file
    assert q.save() == path


def test_project_not_a_binary(tmp_path):
    assert not store.is_project(sample("hello_x64.elf"))
    assert not store.is_project(str(tmp_path / "missing.rvproj"))


def test_corrupted_project(tmp_path):
    bad = tmp_path / "bad.rvproj"
    bad.write_bytes(b"SQLite format 3\x00" + b"garbage" * 50)
    with pytest.raises(ProjectError):
        store.load_project(str(bad))
    with pytest.raises(ProjectError):
        store.load_project(str(tmp_path / "nope.rvproj"))


def test_newer_schema_is_rejected(tmp_path):
    p = fresh("hello_x64.elf")
    path = p.save(str(tmp_path / "h.rvproj"))
    con = sqlite3.connect(path)
    with con:
        con.execute("UPDATE meta SET value='99' WHERE key='schema_version'")
    con.close()
    with pytest.raises(ProjectError, match="novšou verziou"):
        Session.open_project(path, load_plugins=False)


def test_tampered_binary_detected(tmp_path):
    p = fresh("hello_x64.elf")
    path = p.save(str(tmp_path / "h.rvproj"))
    con = sqlite3.connect(path)
    with con:
        con.execute("UPDATE binary SET data = ?", (b"\x7fELF" + b"\0" * 100,))
    con.close()
    with pytest.raises(ProjectError, match="SHA-256"):
        store.load_project(path)


def test_save_error_is_friendly(tmp_path):
    p = fresh("hello_x64.elf")
    with pytest.raises(ProjectError):
        p.save(str(tmp_path / "no" / "such" / "dir" / "x.rvproj"))


def test_default_project_path(tmp_path, monkeypatch):
    target = tmp_path / "bins"
    target.mkdir()
    assert default_project_path(str(target / "a.out")) == str(target / "a.out.rvproj")
    from reversa.cli import demo_path
    assert "reversa-projects" in default_project_path(demo_path())


def test_migrations_applied(tmp_path, monkeypatch):
    p = fresh("hello_x64.elf")
    path = p.save(str(tmp_path / "h.rvproj"))
    monkeypatch.setattr(store, "SCHEMA_VERSION", 2)
    monkeypatch.setitem(store.MIGRATIONS, 2, "CREATE TABLE IF NOT EXISTS extra (x INTEGER);")
    data = store.load_project(path)
    assert data["name"] == "hello_x64.elf"
    con = sqlite3.connect(path)
    assert con.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()[0] == "2"
    con.close()
