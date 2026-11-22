#!/usr/bin/env python3
"""GUI translations: check that every translatable GUI string has a translation in each language.

Translatable strings in src/gui are the literals passed to tr(...), T(...) and QT_TR_NOOP(...).
The source language is Slovak; translations live in src/gui/i18n/<lang>.json as {"source": "translation"}.

  python3 scripts/i18n.py check     # CI: missing/stale keys, empty values, %1 placeholders differ -> exit 1
  python3 scripts/i18n.py missing   # print the missing keys per language as JSON (for translators)
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUI = os.path.join(ROOT, "src", "gui")
I18N = os.path.join(GUI, "i18n")
LANGS = ["en", "de", "fr", "zh"]

CALL = re.compile(r"\b(?:tr|T|QT_TR_NOOP)\(\s*\"")
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}


def _literals(src: str, pos: int):
    """Parse adjacent C++ string literals starting at src[pos] == '"'; return (text, end)."""
    out = []
    while pos < len(src) and src[pos] == '"':
        pos += 1
        while src[pos] != '"':
            if src[pos] == "\\":
                out.append(ESCAPES[src[pos + 1]])
                pos += 2
            else:
                out.append(src[pos])
                pos += 1
        pos += 1
        while pos < len(src) and src[pos] in " \t\r\n":
            pos += 1
    return "".join(out), pos


def extract() -> dict:
    """source string -> first 'file:line' where it appears."""
    found = {}
    for name in sorted(os.listdir(GUI)):
        if not name.endswith((".cpp", ".hpp")):
            continue
        src = open(os.path.join(GUI, name), encoding="utf-8").read()
        for m in CALL.finditer(src):
            text, _ = _literals(src, m.end() - 1)
            found.setdefault(text, f"src/gui/{name}:{src.count(chr(10), 0, m.start()) + 1}")
    return found


def placeholders(s: str):
    return sorted(re.findall(r"%\d", s))


def load(lang: str) -> dict:
    with open(os.path.join(I18N, f"{lang}.json"), encoding="utf-8") as f:
        return json.load(f)


def check() -> int:
    keys = extract()
    errors = []
    for lang in LANGS:
        tr = load(lang)
        for k, where in keys.items():
            if k not in tr:
                errors.append(f"{lang}: chýba preklad ({where}): {k[:70]!r}")
            elif not tr[k].strip():
                errors.append(f"{lang}: prázdny preklad ({where}): {k[:70]!r}")
            elif placeholders(tr[k]) != placeholders(k):
                errors.append(f"{lang}: iné zástupné znaky %N ({where}): {k[:70]!r}")
        for k in tr:
            if k not in keys:
                errors.append(f"{lang}: nepoužívaný kľúč: {k[:70]!r}")
    for e in errors:
        print(e)
    print(f"{len(keys)} textov, {len(LANGS)} jazyky: " + ("OK" if not errors else f"{len(errors)} chýb"))
    return 1 if errors else 0


def missing() -> int:
    keys = extract()
    out = {lang: [k for k in keys if k not in load(lang)] for lang in LANGS}
    json.dump(out, sys.stdout, ensure_ascii=False, indent=1)
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    sys.exit({"check": check, "missing": missing}[cmd]())
