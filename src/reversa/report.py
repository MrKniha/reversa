"""Text report of an analysis (Markdown or self-contained HTML)."""
from __future__ import annotations

import datetime
import html

from reversa import __version__

MAX_ROWS = 300


def _md_table(headers: list[str], rows: list[list]) -> str:
    def cell(v) -> str:
        return str(v).replace("|", "\\|").replace("\n", " ")
    out = ["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)]
    out += ["| " + " | ".join(cell(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def build_markdown(session) -> str:
    a = session.require_analysis()
    b = session.binary
    name = session.name_of
    funcs = sorted((f for f in a.functions.values() if f.kind == "normal"), key=lambda f: f.addr)
    imps = sorted((f for f in a.functions.values() if f.kind != "normal"), key=lambda f: name(f.addr))
    lines = [
        f"# Analýza: {b.path.split('/')[-1]}",
        "",
        f"Vygenerované nástrojom REVERSA {__version__} dňa {datetime.date.today().isoformat()}. "
        "Pseudokód je dočasný (MVP) – overte ho v listingu.",
        "",
        "## Prehľad",
        "",
        _md_table(["Vlastnosť", "Hodnota"], [
            ["Súbor", b.path], ["SHA-256", b.sha256], ["Veľkosť", f"{len(b.data)} B"],
            ["Formát", b.format + (f" ({b.meta.get('elf_type')})" if b.meta.get("elf_type") else "")],
            ["Architektúra", f"{b.arch}, {b.bits}-bit, {b.endian} endian"], ["OS", b.os or "?"],
            ["Kompilátor (odhad)", b.compiler],
            ["Vstupný bod (entry)", f"0x{b.entry:x}" if b.entry else "—"],
            ["Hlavná funkcia", f"{name(a.main)} @ 0x{a.main:x}" if a.main else "nenájdená"],
            ["Funkcie / importy / reťazce", f"{len(funcs)} / {len(imps)} / {len(a.strings)}"],
            ["Čas analýzy", f"{a.duration:.2f} s"],
        ]),
        "",
    ]
    if session.diagnostics:
        lines += ["## Upozornenia pri načítaní", ""]
        lines += [f"- **{d.severity}**: {d.friendly()}" for d in session.diagnostics]
        lines.append("")
    lines += ["## Sekcie", "", _md_table(["Meno", "Adresa", "Veľkosť", "Práva", "Typ"],
              [[s.name, f"0x{s.addr:x}", s.size, s.perms, s.kind] for s in b.sections]), ""]
    lines += ["## Importy", "", _md_table(["Meno", "Knižnica", "Volaní z"],
              [[name(f.addr), f.library or "—", len(a.callers(f.addr))] for f in imps][:MAX_ROWS]), ""]
    if b.exports:
        lines += ["## Exporty", "", _md_table(["Meno", "Adresa"], [[e.name, f"0x{e.addr:x}"] for e in b.exports][:MAX_ROWS]), ""]
    lines += ["## Funkcie", "", _md_table(["Meno", "Adresa", "Veľkosť", "Volaná z", "Volá"],
              [[name(f.addr), f"0x{f.addr:x}", f.size, len(a.callers(f.addr)), ", ".join(name(c) for c in sorted(f.callees))[:120]]
               for f in funcs][:MAX_ROWS]), ""]
    lines += ["## Reťazce (výber)", "", _md_table(["Adresa", "Kódovanie", "Text", "Odkazy"],
              [[f"0x{s.addr:x}", s.encoding, s.text[:80], len(a.xrefs.to(s.addr))]
               for s in sorted(a.strings, key=lambda s: -len(a.xrefs.to(s.addr)))[:100]]), ""]
    if a.main:
        lines += [f"## Pseudokód hlavnej funkcie `{name(a.main)}`", "", "```c", session.pseudocode(a.main), "```", ""]
    ann = session.annotations
    if ann.names or ann.comments or ann.bookmarks:
        lines += ["## Anotácie používateľa", ""]
        lines += [f"- 0x{k:x}: premenované na `{v}`" for k, v in sorted(ann.names.items())]
        lines += [f"- 0x{k:x}: komentár „{v}“" for k, v in sorted(ann.comments.items())]
        lines += [f"- 0x{k:x}: záložka „{v}“" for k, v in sorted(ann.bookmarks.items())]
        lines.append("")
    for vname, render in session.views.items():
        try:
            lines += [f"## Pohľad: {vname}", "", str(render(session.plugins.api)), ""]
        except Exception as exc:
            lines += [f"## Pohľad: {vname}", "", f"(chyba pri vytváraní pohľadu: {exc})", ""]
    return "\n".join(lines)


def build_html(session) -> str:
    md = build_markdown(session)
    body, in_code, in_table = [], False, False
    for line in md.splitlines():
        if line.startswith("```"):
            body.append("</pre>" if in_code else "<pre>")
            in_code = not in_code
            continue
        if in_code:
            body.append(html.escape(line))
            continue
        if line.startswith("|"):
            if line.startswith("|---"):
                continue
            cells = [html.escape(c.strip().replace("\\|", "|")) for c in line.strip("|").split(" | ")]
            if not in_table:
                body.append("<table><tr>" + "".join(f"<th>{c}</th>" for c in cells) + "</tr>")
                in_table = True
            else:
                body.append("<tr>" + "".join(f"<td>{c}</td>" for c in cells) + "</tr>")
            continue
        if in_table:
            body.append("</table>")
            in_table = False
        if line.startswith("## "):
            body.append(f"<h2>{html.escape(line[3:])}</h2>")
        elif line.startswith("# "):
            body.append(f"<h1>{html.escape(line[2:])}</h1>")
        elif line.startswith("- "):
            body.append(f"<li>{html.escape(line[2:]).replace('**', '')}</li>")
        elif line:
            body.append(f"<p>{html.escape(line)}</p>")
    if in_table:
        body.append("</table>")
    style = ("body{font-family:system-ui,sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#1d2330}"
             "table{border-collapse:collapse;margin:.5rem 0}td,th{border:1px solid #ccd;padding:.25rem .5rem;font-size:.9rem}"
             "th{background:#eef}pre{background:#f4f5f8;padding:1rem;overflow:auto}"
             "@media(prefers-color-scheme:dark){body{background:#14161c;color:#dde}th{background:#252a38}pre{background:#1c1f28}td,th{border-color:#334}}")
    return f"<!doctype html><html lang=sk><meta charset=utf-8><title>REVERSA report</title><style>{style}</style><body>{''.join(body)}</body></html>"


def build_report(session, fmt: str = "md") -> str:
    return build_html(session) if fmt == "html" else build_markdown(session)
