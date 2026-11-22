#!/usr/bin/env python3
"""CI license gate: every runtime dependency must have an allowed license and be
listed in LICENSE-THIRD-PARTY. Uses only installed package metadata (no network).

Exit code 0 = OK, 1 = problem (message explains what to fix).
"""
import os
import re
import sys
from importlib import metadata

ALLOWED = {"MIT", "BSD", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "ISC", "PSF-2.0", "Python-2.0", "Unlicense"}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def runtime_requirements(name: str) -> list[str]:
    reqs = metadata.requires(name) or []
    out = []
    for r in reqs:
        if "extra ==" in r or not marker_applies(r):
            continue
        out.append(re.split(r"[ <>=!~;\[]", r, maxsplit=1)[0])
    return out


def marker_applies(req: str) -> bool:
    """Tiny evaluator for the only marker form we meet: python_version <op> "X.Y"."""
    m = re.search(r'python_version\s*(<=|>=|<|>|==|!=)\s*"(\d+)\.(\d+)"', req)
    if not m:
        return True
    cur, want = sys.version_info[:2], (int(m.group(2)), int(m.group(3)))
    return {"<": cur < want, "<=": cur <= want, ">": cur > want, ">=": cur >= want, "==": cur == want, "!=": cur != want}[m.group(1)]


def license_of(dist: str) -> str:
    md = metadata.metadata(dist)
    expr = md.get("License-Expression")
    if expr:
        return expr
    for c in md.get_all("Classifier") or []:
        if c.startswith("License :: OSI Approved :: "):
            v = c.rsplit("::", 1)[1].strip()
            return {"MIT License": "MIT", "BSD License": "BSD", "Apache Software License": "Apache-2.0",
                    "ISC License (ISCL)": "ISC"}.get(v, v)
    return (md.get("License") or "UNKNOWN").strip().splitlines()[0][:60]


def main() -> int:
    third = open(os.path.join(ROOT, "LICENSE-THIRD-PARTY"), encoding="utf-8").read().lower()
    seen, todo, problems = set(), runtime_requirements("reversa"), []
    while todo:
        dep = todo.pop()
        if dep.lower() in seen:
            continue
        seen.add(dep.lower())
        try:
            lic = license_of(dep)
        except metadata.PackageNotFoundError:
            problems.append(f"{dep}: nie je nainštalovaný – nedá sa overiť licencia")
            continue
        ok = any(a.lower() in lic.lower() for a in ALLOWED)
        listed = dep.lower() in third
        print(f"{dep:<20} {lic:<30} {'OK' if ok else 'NEPOVOLENÁ'}  {'uvedený' if listed else 'CHÝBA v LICENSE-THIRD-PARTY'}")
        if not ok:
            problems.append(f"{dep}: licencia '{lic}' nie je na zozname povolených {sorted(ALLOWED)}")
        if not listed:
            problems.append(f"{dep}: doplňte do LICENSE-THIRD-PARTY")
        todo += runtime_requirements(dep)
    for p in problems:
        print("CHYBA:", p, file=sys.stderr)
    print("Licenčná kontrola:", "PREŠLA" if not problems else "NEPREŠLA")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # Windows pipes default to cp1252
    sys.exit(main())
