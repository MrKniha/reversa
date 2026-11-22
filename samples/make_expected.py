"""Regenerates samples/expected/*.json (golden files) from the current analysis.

Run only after verifying by hand that a change in results is correct:
    python samples/make_expected.py && git diff samples/expected
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "src"))
import reversa  # noqa: E402

USER_FUNCS = {"hello": ["main", "sum_to", "classify", "greet"], "login": ["main", "check_password", "check_user"]}


def facts(path: str) -> dict:
    prog = reversa.open(path, load_plugins=False)
    s = prog.session
    a = s.analysis
    base = os.path.basename(path).split("_")[0]
    names = {f.name.lstrip("_") for f in prog.functions(include_imports=True)}
    imports = sorted({s.name_of(f.addr).lstrip("_") for f in a.functions.values() if f.kind != "normal"} &
                     {"printf", "puts", "strcmp", "strcpy", "strcat", "strlen", "__libc_start_main"})
    return {
        "format": prog.format, "arch": prog.arch,
        "main_found": prog.main is not None,
        "user_functions_present": sorted(n for n in USER_FUNCS[base] if n in names),
        "imports_present": imports,
        "strings_present": sorted(t for t in ("Done.", "Usage: login <user> <password>", "admin", "student",
                                               "Access granted. Flag: CTF{xrefs_are_your_friend}", "sum_to(%d) = %d\n")
                                  if any(x.text == t for x in a.strings)),
        "strcmp_callers": sorted(f.name.lstrip("_") for f in prog.callers_of("strcmp")) if s.resolve("strcmp") else [],
        "main_calls": sorted(s.name_of(c).lstrip("_") for c in a.functions[a.main].callees) if a.main else [],
    }


if __name__ == "__main__":
    out = os.path.join(HERE, "expected")
    os.makedirs(out, exist_ok=True)
    for name in sorted(os.listdir(os.path.join(HERE, "bin"))):
        if name.endswith("SUMS"):
            continue
        with open(os.path.join(out, name + ".json"), "w") as fh:
            json.dump(facts(os.path.join(HERE, "bin", name)), fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        print("wrote", name)
