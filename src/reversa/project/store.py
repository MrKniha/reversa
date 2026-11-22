"""SQLite project format.

Why SQLite: one portable file, transactional (no half-written projects after
a crash), readable by any tool, and the stdlib ships it. The schema carries a
version number; ``MIGRATIONS`` upgrades old projects step by step. Ghidra uses
a similar "database + versioned schema" approach for its .gpr/.rep projects.
"""
from __future__ import annotations

import hashlib
import json
import os
import sqlite3
import time
from pathlib import Path

from reversa.core.errors import ProjectError
from reversa.project.annotations import Annotations

SCHEMA_VERSION = 1
MAGIC = b"SQLite format 3\x00"

SCHEMA_V1 = """
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE binary (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, path TEXT, sha256 TEXT, data BLOB, load_options TEXT);
CREATE TABLE names (addr INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE comments (addr INTEGER PRIMARY KEY, text TEXT NOT NULL);
CREATE TABLE bookmarks (addr INTEGER PRIMARY KEY, label TEXT NOT NULL);
CREATE TABLE var_names (func INTEGER, key TEXT, name TEXT NOT NULL, PRIMARY KEY (func, key));
"""

# version -> SQL that upgrades from version-1 to version. Empty for now; add
# entries here (never edit old ones) when the schema changes.
MIGRATIONS: dict[int, str] = {}


def is_project(path: str) -> bool:
    return _has_meta(path)


def _has_meta(path: str) -> bool:
    try:
        with open(path, "rb") as fh:
            if fh.read(16) != MAGIC:
                return False
        con = sqlite3.connect(Path(path).resolve().as_uri() + "?mode=ro", uri=True)  # as_uri: Windows drive letters, %-escaping
        try:
            con.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()
            return True
        finally:
            con.close()
    except (sqlite3.Error, OSError):
        return False


def save_project(path: str, data: bytes, binary_path: str, load_options: dict, ann: Annotations, settings: dict | None = None) -> None:
    tmp = path + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    try:
        con = sqlite3.connect(tmp)
        with con:
            con.executescript(SCHEMA_V1)
            meta = {"schema_version": str(SCHEMA_VERSION), "app": "REVERSA", "saved_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
                    "settings": json.dumps(settings or {})}
            con.executemany("INSERT INTO meta VALUES (?, ?)", meta.items())
            con.execute("INSERT INTO binary VALUES (1, ?, ?, ?, ?, ?)", (os.path.basename(binary_path), binary_path,
                        hashlib.sha256(data).hexdigest(), data, json.dumps(load_options)))
            con.executemany("INSERT INTO names VALUES (?, ?)", ann.names.items())
            con.executemany("INSERT INTO comments VALUES (?, ?)", ann.comments.items())
            con.executemany("INSERT INTO bookmarks VALUES (?, ?)", ann.bookmarks.items())
            con.executemany("INSERT INTO var_names VALUES (?, ?, ?)", [(f, k, n) for (f, k), n in ann.var_names.items()])
        con.close()
        os.replace(tmp, path)  # atomic: an interrupted save never corrupts the old project
    except (sqlite3.Error, OSError) as exc:
        raise ProjectError(f"Projekt sa nepodarilo uložiť do '{path}'.", str(exc)) from exc


def load_project(path: str) -> dict:
    if not os.path.exists(path):
        raise ProjectError(f"Projekt '{path}' neexistuje.")
    try:
        con = sqlite3.connect(path)
    except sqlite3.Error as exc:
        raise ProjectError(f"'{path}' nie je platný projekt REVERSA.", str(exc)) from exc
    try:
        row = con.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()
        version = int(row[0])
        if version > SCHEMA_VERSION:
            raise ProjectError(f"Projekt bol uložený novšou verziou REVERSA (schéma {version}). Aktualizujte REVERSA.")
        for v in range(version + 1, SCHEMA_VERSION + 1):
            with con:
                con.executescript(MIGRATIONS[v])
                con.execute("UPDATE meta SET value=? WHERE key='schema_version'", (str(v),))
        name, bpath, sha, data, opts = con.execute("SELECT name, path, sha256, data, load_options FROM binary WHERE id=1").fetchone()
        if hashlib.sha256(data).hexdigest() != sha:
            raise ProjectError("Binárka uložená v projekte je poškodená (nesedí kontrolný súčet SHA-256).")
        ann = Annotations(
            names=dict(con.execute("SELECT addr, name FROM names")),
            comments=dict(con.execute("SELECT addr, text FROM comments")),
            bookmarks=dict(con.execute("SELECT addr, label FROM bookmarks")),
            var_names={(f, k): n for f, k, n in con.execute("SELECT func, key, name FROM var_names")},
        )
        settings = json.loads(dict(con.execute("SELECT key, value FROM meta")).get("settings", "{}"))
        return {"name": name, "path": bpath, "data": bytes(data), "load_options": json.loads(opts or "{}"),
                "annotations": ann, "settings": settings}
    except ProjectError:
        raise
    except (sqlite3.Error, TypeError, ValueError, KeyError) as exc:
        raise ProjectError(f"Projekt '{path}' je poškodený alebo nie je to projekt REVERSA.", str(exc)) from exc
    finally:
        con.close()
