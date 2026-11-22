#!/usr/bin/env python3
"""CI license gate for the C++ build.

Collects every third-party component the build pulls in (FetchContent_Declare in CMakeLists.txt,
vcpkg.json dependencies, find_package(Qt6/Python/SQLite3)) and the locked test tools, then checks
that each one has a known, allowed license and is listed in LICENSE-THIRD-PARTY. No network access.

Exit code 0 = OK, 1 = problem (message explains what to fix).
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Known components: name -> (license, name as written in LICENSE-THIRD-PARTY).
KNOWN = {
    "capstone": ("BSD-3-Clause", "Capstone"),
    "pybind11": ("BSD-3-Clause", "pybind11"),
    "sqlite3": ("Public-Domain", "SQLite"),
    "qt6": ("LGPL-3.0-only", "Qt"),
    "python": ("PSF-2.0", "Python"),
    "pytest": ("MIT", "pytest"),
    "iniconfig": ("MIT", "iniconfig"),
    "packaging": ("Apache-2.0", "packaging"),
    "pluggy": ("MIT", "pluggy"),
    "colorama": ("BSD-3-Clause", "colorama"),
}
# LGPL is allowed only because Qt is linked dynamically and shipped as separate, replaceable DLLs/.so.
ALLOWED = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "ISC", "PSF-2.0", "Public-Domain", "LGPL-3.0-only"}


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding="utf-8") as fh:
        return fh.read()


def components() -> list[str]:
    cmake = read("CMakeLists.txt")
    found = [m.lower() for m in re.findall(r"FetchContent_Declare\(\s*(\w+)", cmake)]
    for pkg, name in (("Qt6", "qt6"), ("Python", "python"), ("SQLite3", "sqlite3")):
        if re.search(rf"find_package\(\s*{pkg}\b", cmake):
            found.append(name)
    found += [d if isinstance(d, str) else d["name"] for d in json.loads(read("vcpkg.json")).get("dependencies", [])]
    found += re.findall(r"^([A-Za-z0-9_.-]+)==", read("tests/requirements-dev.lock"), re.M)
    return sorted(set(n.lower() for n in found))


def main() -> int:
    third = read("LICENSE-THIRD-PARTY").lower()
    problems = []
    for dep in components():
        lic, listed_as = KNOWN.get(dep, ("UNKNOWN", dep))
        ok = lic in ALLOWED
        listed = listed_as.lower() in third
        print(f"{dep:<12} {lic:<16} {'OK' if ok else 'NEPOVOLENÁ'}  {'uvedený' if listed else 'CHÝBA v LICENSE-THIRD-PARTY'}")
        if dep not in KNOWN:
            problems.append(f"{dep}: neznáma závislosť – doplňte jej licenciu do scripts/check_licenses.py")
        elif not ok:
            problems.append(f"{dep}: licencia '{lic}' nie je na zozname povolených {sorted(ALLOWED)}")
        if not listed:
            problems.append(f"{dep}: doplňte '{listed_as}' do LICENSE-THIRD-PARTY")
    for p in problems:
        print("CHYBA:", p, file=sys.stderr)
    print("Licenčná kontrola:", "PREŠLA" if not problems else "NEPREŠLA")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # Windows pipes default to cp1252
    sys.exit(main())
