"""Command-line interface: ``reversa``.

    reversa --demo                 open the demo binary in the GUI (add --no-gui for terminal output)
    reversa gui [FILE]             start the GUI (local web app on 127.0.0.1)
    reversa analyze FILE [--report out.md|out.html] [--json] [--project out.rvproj]
    reversa info FILE              quick header information
    reversa search FILE (--string|--hex|--insn|--code) QUERY
    reversa script SCRIPT.py [ARGS...]
"""
from __future__ import annotations

import argparse
import json
import os
import runpy
import sys
import time
import traceback

from reversa import __version__
from reversa.core.errors import ReversaError

EXIT_OK, EXIT_ERROR, EXIT_USAGE = 0, 1, 2


def demo_path() -> str:
    return os.path.join(os.path.dirname(__file__), "demo", "login_x64.elf")


def _int(v: str) -> int:
    return int(v, 0)


def _common(p: argparse.ArgumentParser) -> None:
    p.add_argument("--arch", choices=["x86", "x86-64", "arm", "arm64"], help="ručne nastavená architektúra (pre surové dáta)")
    p.add_argument("--base", type=_int, help="základná adresa pre surové dáta, napr. 0x8000000")
    p.add_argument("--raw", action="store_true", help="načítať ako surové dáta (ignorovať hlavičky)")
    p.add_argument("--plugins", action="append", default=None, metavar="DIR", help="priečinok s pluginmi (môže sa opakovať)")
    p.add_argument("--no-plugins", action="store_true", help="nenačítať žiadne pluginy")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="reversa", description="REVERSA – open-source nástroj na reverzné inžinierstvo.")
    p.add_argument("--version", action="version", version=f"REVERSA {__version__}")
    p.add_argument("--demo", action="store_true", help="spustiť ukážkovú analýzu (v GUI, s --no-gui v termináli)")
    p.add_argument("--no-gui", action="store_true", help="pri --demo vypísať výsledky do terminálu")
    p.add_argument("--no-browser", action="store_true", help="neotvárať prehliadač (len vypísať adresu)")
    p.add_argument("--port", type=int, default=0, help="port GUI servera (predvolene náhodný voľný)")
    sub = p.add_subparsers(dest="cmd")

    a = sub.add_parser("analyze", help="analyzovať súbor bez GUI (headless)")
    a.add_argument("file")
    a.add_argument("--report", metavar="FILE", help="uložiť report (.md alebo .html)")
    a.add_argument("--json", action="store_true", help="vypísať súhrn ako JSON")
    a.add_argument("--project", metavar="FILE", help="uložiť projekt (.rvproj)")
    a.add_argument("--pseudocode", metavar="FUNC", help="vypísať pseudokód funkcie (meno alebo adresa)")
    a.add_argument("--fail-on-error", action="store_true", help="návratový kód 1, ak boli chyby pri načítaní")
    _common(a)

    g = sub.add_parser("gui", help="spustiť grafické rozhranie")
    g.add_argument("file", nargs="?")
    _common(g)

    i = sub.add_parser("info", help="základné informácie o súbore")
    i.add_argument("file")
    _common(i)

    s = sub.add_parser("search", help="hľadať v súbore")
    s.add_argument("file")
    grp = s.add_mutually_exclusive_group(required=True)
    grp.add_argument("--string", metavar="TEXT")
    grp.add_argument("--hex", metavar="PATTERN", help="napr. '48 8b ?? 05'")
    grp.add_argument("--insn", metavar="REGEX", help="regex cez inštrukcie, napr. 'call.*strcmp'")
    grp.add_argument("--code", metavar="REGEX", help="regex cez pseudokód")
    s.add_argument("--regex", action="store_true", help="--string ako regulárny výraz")
    _common(s)

    sc = sub.add_parser("script", help="spustiť Python skript s dostupným modulom reversa")
    sc.add_argument("script")
    sc.add_argument("args", nargs=argparse.REMAINDER)
    return p


def _open(args, analyze: bool = True):
    from reversa.api.program import open_program
    dirs = [] if getattr(args, "no_plugins", False) else (getattr(args, "plugins", None) or None)
    if dirs is not None and getattr(args, "plugins", None):
        from reversa.plugins.manager import default_dirs
        dirs = default_dirs() + dirs
    return open_program(args.file, analyze, getattr(args, "arch", None), getattr(args, "base", None),
                        getattr(args, "raw", False), dirs, not getattr(args, "no_plugins", False))


def _progress(frac: float, msg: str) -> None:
    sys.stderr.write(f"\r[{int(frac * 100):3d} %] {msg:<50}")
    sys.stderr.flush()
    if frac >= 1:
        sys.stderr.write("\n")


def summary(prog) -> dict:
    s = prog.session
    a = s.analysis
    b = s.binary
    return {
        "file": b.path, "format": b.format, "arch": b.arch, "bits": b.bits, "endian": b.endian, "compiler": b.compiler,
        "entry": hex(b.entry) if b.entry else None, "main": s.name_of(a.main) if a.main else None,
        "functions": sum(1 for f in a.functions.values() if f.kind == "normal"),
        "imports": sum(1 for f in a.functions.values() if f.kind != "normal"),
        "strings": len(a.strings), "xrefs": len(a.xrefs), "seconds": round(a.duration, 3),
        "diagnostics": [{"severity": d.severity, "message": d.friendly()} for d in s.diagnostics],
    }


def cmd_analyze(args) -> int:
    from reversa.api.program import open_program  # noqa: F401
    prog = _open(args, analyze=False)
    prog.session.analyze(progress=_progress if sys.stderr.isatty() else None)
    info = summary(prog)
    if args.json:
        print(json.dumps(info, ensure_ascii=False, indent=2))
    else:
        print(f"Súbor:        {info['file']}")
        print(f"Formát:       {info['format']} / {info['arch']} ({info['bits']}-bit, {info['endian']})")
        print(f"Kompilátor:   {info['compiler']}")
        print(f"Funkcie:      {info['functions']} (+ {info['imports']} importov), reťazce: {info['strings']}, x-refs: {info['xrefs']}")
        print(f"Hlavná fn.:   {info['main'] or 'nenájdená'}   (analýza {info['seconds']} s)")
        for d in info["diagnostics"]:
            print(f"  ! {d['severity']}: {d['message']}")
    if args.pseudocode:
        print(prog.decompile(args.pseudocode))
    if args.report:
        prog.report(args.report)
        print(f"Report uložený: {os.path.abspath(args.report)}")
    if args.project:
        print(f"Projekt uložený: {prog.save(args.project)}")
    if args.fail_on_error and any(d["severity"] == "error" for d in info["diagnostics"]):
        return EXIT_ERROR
    return EXIT_OK


def cmd_info(args) -> int:
    prog = _open(args, analyze=False)
    b = prog.binary
    print(f"{b.format} {b.arch} {b.bits}-bit {b.endian}, entry={hex(b.entry) if b.entry else '-'}, kompilátor: {b.compiler}")
    for s in b.sections:
        print(f"  {s.name:<20} 0x{s.addr:<12x} {s.size:>10}  {s.perms}")
    for d in prog.session.diagnostics:
        print(f"  ! {d.friendly()}")
    return EXIT_OK


def cmd_search(args) -> int:
    prog = _open(args)
    if args.string is not None:
        hits = prog.search_strings(args.string, args.regex)
    elif args.hex is not None:
        hits = [(a, args.hex) for a in prog.search_bytes(args.hex)]
    elif args.insn is not None:
        hits = prog.search_instructions(args.insn)
    else:
        hits = prog.search_pseudocode(args.code)
    for addr, text in hits:
        print(f"0x{addr:x}  {text}")
    print(f"{len(hits)} výsledkov", file=sys.stderr)
    return EXIT_OK


def cmd_script(args) -> int:
    sys.argv = [args.script] + args.args
    runpy.run_path(args.script, run_name="__main__")
    return EXIT_OK


def cmd_demo_terminal() -> int:
    from reversa.api.program import open_program
    t0 = time.perf_counter()
    prog = open_program(demo_path(), load_plugins=False)
    info = summary(prog)
    print("REVERSA demo – ukážková binárka 'login' (CTF úloha: nájdite heslo)\n")
    print(f"Formát {info['format']} {info['arch']}, {info['functions']} funkcií, {info['strings']} reťazcov, "
          f"analýza {info['seconds']} s\n")
    print("Funkcie, ktoré volajú strcmp (porovnanie reťazcov):")
    for f in prog.callers_of("strcmp"):
        print(f"  {f.name:<20} 0x{f.addr:x}")
    print(f"\nPseudokód hlavnej funkcie '{prog.main.name}':\n")
    print(prog.main.pseudocode())
    print(f"\nHotovo za {time.perf_counter() - t0:.2f} s. GUI spustíte príkazom: reversa --demo")
    return EXIT_OK


def main(argv: list[str] | None = None) -> int:
    for stream in (sys.stdout, sys.stderr):  # Windows consoles/pipes default to cp1252, which lacks Slovak letters
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.demo and args.no_gui:
            return cmd_demo_terminal()
        if args.demo or args.cmd == "gui":
            from reversa.ui.server import run_gui
            path = demo_path() if args.demo else getattr(args, "file", None)
            opts = {k: getattr(args, k, None) for k in ("arch", "base", "raw", "plugins", "no_plugins")}
            return run_gui(path, port=args.port, open_browser=not args.no_browser, options=opts)
        if args.cmd == "analyze":
            return cmd_analyze(args)
        if args.cmd == "info":
            return cmd_info(args)
        if args.cmd == "search":
            return cmd_search(args)
        if args.cmd == "script":
            return cmd_script(args)
        parser.print_help()
        return EXIT_USAGE
    except ReversaError as exc:
        print(f"Chyba: {exc.message}", file=sys.stderr)
        if exc.detail:
            print(f"Detail pre správcu: {exc.detail.splitlines()[-1] if exc.detail else ''}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("\nPrerušené.", file=sys.stderr)
        return 130
    except Exception as exc:  # last resort: friendly message + details
        print(f"Neočakávaná chyba: {type(exc).__name__}: {exc}\n"
              "Prosím, nahláste ju (SECURITY.md / issues) aj s týmto detailom:", file=sys.stderr)
        traceback.print_exc()
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
