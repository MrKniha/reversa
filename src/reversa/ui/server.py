"""Local GUI server: JSON API over the Session + static single-page frontend.

Security: binds to 127.0.0.1 only, requires a random per-run token on every
API call (prevents other web pages from driving the API via the browser) and
rejects foreign Host headers (DNS rebinding). No network access is made.
"""
from __future__ import annotations

import bisect
import json
import mimetypes
import os
import secrets
import tempfile
import threading
import traceback
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional

from reversa import __version__
from reversa.analysis.graphs import build_cfg, call_graph
from reversa.core.errors import Cancelled, ReversaError
from reversa.disasm.base import FLOW_CALL, FLOW_CJMP, FLOW_ICALL, FLOW_IJMP, FLOW_JMP, FLOW_RET

STATIC = os.path.join(os.path.dirname(__file__), "static")
I18N = os.path.join(os.path.dirname(__file__), "i18n")
DOCS_CANDIDATES = [os.path.join(os.path.dirname(__file__), "..", "..", "..", "docs"),
                   os.path.join(os.path.dirname(__file__), "docs")]
MAX_UPLOAD = 256 * 1024 * 1024


class GuiState:
    def __init__(self, options: Optional[dict] = None):
        self.options = options or {}
        self.session = None
        self.status = "empty"  # empty | loading | analyzing | ready | error
        self.progress = 0.0
        self.message = ""
        self.error: Optional[dict] = None
        self.cancel = threading.Event()
        self.thread: Optional[threading.Thread] = None
        self.lock = threading.RLock()
        self.toast: Optional[str] = None

    # ------------------------------------------------------------------
    def open(self, path: str, display_name: Optional[str] = None) -> None:
        with self.lock:
            if self.thread and self.thread.is_alive():
                self.cancel.set()
                self.thread.join(timeout=5)
            self.cancel = threading.Event()
            self.status, self.progress, self.message, self.error = "loading", 0.0, "Načítavam súbor", None
            self.thread = threading.Thread(target=self._worker, args=(path, display_name), daemon=True)
            self.thread.start()

    def _worker(self, path: str, display_name: Optional[str]) -> None:
        from reversa.plugins.manager import default_dirs
        from reversa.session import Session
        o = self.options
        dirs = [] if o.get("no_plugins") else default_dirs() + list(o.get("plugins") or [])
        try:
            s = Session.open(path, o.get("arch"), o.get("base"), bool(o.get("raw")), dirs, not o.get("no_plugins"))
            if display_name:
                s.display_name = display_name
            with self.lock:
                self.session = s
                self.status = "analyzing"

            def progress(frac: float, msg: str) -> None:
                self.progress, self.message = frac, msg
            s.analyze(progress, self.cancel)
            self.status, self.progress, self.message = "ready", 1.0, "Analýza dokončená"
        except Cancelled:
            self.status, self.message = ("empty" if self.session is None else "error"), "Analýza zrušená"
            self.error = {"message": "Analýza bola zrušená. Súbor môžete otvoriť znova.", "detail": ""}
            if self.session is not None and self.session.analysis is None:
                self.session = None
                self.status = "empty"
        except ReversaError as exc:
            self.status, self.error = "error", {"message": exc.message, "detail": exc.detail}
        except Exception as exc:
            self.status = "error"
            self.error = {"message": "Pri analýze nastala neočakávaná chyba – načítal som, čo sa dalo.",
                          "detail": "".join(traceback.format_exception(exc))}


# ---------------------------------------------------------------------- helpers
def hx(v: Optional[int]) -> Optional[str]:
    return None if v is None else f"0x{v:x}"


def parse_addr(v) -> int:
    if isinstance(v, int):
        return v
    try:
        return int(str(v), 16) if str(v).lower().startswith("0x") else int(str(v), 0)
    except ValueError:
        raise ReversaError(f"'{v}' nie je platná adresa (príklad: 0x401000).") from None


class Api:
    """Pure functions building JSON responses; unit-testable without HTTP."""

    def __init__(self, state: GuiState):
        self.st = state

    @property
    def s(self):
        if self.st.session is None:
            raise ReversaError("Najprv otvorte súbor alebo spustite demo.")
        return self.st.session

    @property
    def a(self):
        a = self.s.analysis
        if a is None:
            raise ReversaError("Analýza ešte beží – chvíľu počkajte.")
        return a

    # -- state ------------------------------------------------------------
    def state(self, q=None) -> dict:
        st = self.st
        out = {"status": st.status, "progress": round(st.progress, 3), "message": st.message, "error": st.error,
               "version": __version__}
        s = st.session
        if s is not None:
            b = s.binary
            out["file"] = {"name": getattr(s, "display_name", None) or os.path.basename(b.path), "path": b.path,
                           "format": b.format, "arch": b.arch, "bits": b.bits, "endian": b.endian,
                           "compiler": b.compiler, "entry": hx(b.entry), "size": len(b.data), "os": b.os, "sha256": b.sha256,
                           "main": hx(s.analysis.main) if s.analysis else None}
            out["revision"] = s.events.revision
            out["can_undo"], out["can_redo"] = s.undo_stack.can_undo, s.undo_stack.can_redo
            out["dirty"] = s.dirty
            out["project_path"] = s.project_path
            out["diagnostics"] = [{"severity": d.severity, "message": d.friendly(), "detail": d.detail} for d in s.diagnostics]
            out["plugins"] = [p.as_dict() for p in s.plugins.plugins]
            out["views"] = sorted(s.views)
            out["settings"] = s.settings
        return out

    # -- tables -------------------------------------------------------------
    def functions(self, q=None) -> list:
        s, a = self.s, self.a
        out = []
        for f in sorted(a.functions.values(), key=lambda f: f.addr):
            out.append({"addr": hx(f.addr), "name": s.name_of(f.addr), "size": f.size, "kind": f.kind,
                        "callers": len(a.callers(f.addr, through_thunks=False)), "xrefs": a.xrefs.count_to(f.addr),
                        "library": f.library, "user": s.has_user_name(f.addr), "main": f.addr == a.main})
        return out

    def strings(self, q=None) -> list:
        a = self.a
        return [{"addr": hx(x.addr), "text": x.text[:400], "enc": x.encoding, "len": x.length,
                 "xrefs": len(a.xrefs.to(x.addr))} for x in a.strings]

    def imports(self, q=None) -> list:
        s, a = self.s, self.a
        return [{"addr": hx(f.addr), "name": s.name_of(f.addr), "library": f.library, "kind": f.kind,
                 "callers": len(a.callers(f.addr))} for f in sorted(a.functions.values(), key=lambda f: s.name_of(f.addr).lower())
                if f.kind in ("import", "thunk") and f.addr not in a.thunk_target] + \
               [{"addr": None, "name": lib, "library": lib, "kind": "library", "callers": 0} for lib in self.s.binary.libraries
                if not any(i.library == lib for i in self.s.binary.imports)]

    def exports(self, q=None) -> list:
        return [{"addr": hx(e.addr), "name": e.name, "ordinal": e.ordinal, "forwarder": e.forwarder} for e in self.s.binary.exports]

    def sections(self, q=None) -> list:
        return [{"name": x.name, "addr": hx(x.addr), "size": x.size, "offset": hx(x.offset), "file_size": x.file_size,
                 "perms": x.perms, "kind": x.kind} for x in self.s.binary.sections]

    def relocations(self, q=None) -> list:
        return [{"addr": hx(r.addr), "type": r.type, "symbol": r.symbol, "addend": r.addend} for r in self.s.binary.relocations[:20000]]

    def bookmarks(self, q=None) -> list:
        s = self.s
        out = [{"addr": hx(k), "label": v, "type": "bookmark", "name": s.name_of(k)} for k, v in sorted(s.annotations.bookmarks.items())]
        out += [{"addr": hx(k), "label": v, "type": "comment", "name": s.name_of(k)} for k, v in sorted(s.annotations.comments.items())]
        return out

    def hexdump(self, q) -> dict:
        addr = parse_addr(q.get("addr", "0"))
        length = min(int(q.get("len", 512)), 64 * 1024)
        b = self.s.binary
        if not b.is_mapped(addr):
            sec = b.sections[0] if b.sections else None
            if sec is None:
                return {"rows": []}
            addr = sec.addr
        data = b.read(addr, length)
        rows = []
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            rows.append({"addr": hx(addr + i), "hex": " ".join(f"{c:02x}" for c in chunk),
                         "ascii": "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)})
        return {"addr": hx(addr), "rows": rows, "offset": hx(b.addr_to_offset(addr))}

    # -- listing ------------------------------------------------------------
    def _insn_line(self, ins) -> dict:
        s, a = self.s, self.a
        line = {"type": "insn", "addr": hx(ins.addr), "bytes": ins.raw.hex(" ")[:30], "mn": ins.mnemonic, "ops": ins.op_str,
                "flow": ins.flow, "refs": []}
        tgt = None
        if ins.flow in (FLOW_CALL, FLOW_JMP, FLOW_CJMP, FLOW_ICALL, FLOW_IJMP):
            for x in a.xrefs.frm(ins.addr):
                if x.kind in ("call", "jump"):
                    tgt = x.to
            tgt = tgt if tgt is not None else ins.target
        if tgt is not None:
            line["target"] = {"addr": hx(tgt), "name": s.name_of(tgt)}
        for ref, kind in ins.refs:
            r = {"addr": hx(ref), "name": s.name_of(ref), "kind": kind}
            txt = s.string_text(ref)
            if txt is not None:
                r["string"] = txt[:80]
            line["refs"].append(r)
        c = s.annotations.comments.get(ins.addr)
        if c:
            line["comment"] = c
        if ins.addr in s.annotations.bookmarks:
            line["bookmark"] = s.annotations.bookmarks[ins.addr]
        return line

    def listing(self, q) -> dict:
        s, a = self.s, self.a
        b = s.binary
        addr = parse_addr(q.get("addr") or (hx(a.main) if a.main else None) or hx(b.entry) or hx(b.sections[0].addr if b.sections else 0))
        count = min(int(q.get("count", 200)), 2000)
        direction = q.get("dir", "down")
        sec = b.section_at(addr)
        if sec is None:
            raise ReversaError(f"Adresa 0x{addr:x} nie je v žiadnej sekcii súboru.")
        if direction == "up":
            if sec.executable:
                known = self._sorted_insns()
                i = bisect.bisect_left(known, addr)
                start = known[max(0, i - count)] if i > 0 else max(sec.addr, addr - count * 4)
                start = max(start, sec.addr)
            else:
                start = max(sec.addr, addr - count * 16)
            lines = self._lines_from(start, count, stop=addr)
        else:
            lines = self._lines_from(addr, count)
        return {"addr": hx(addr), "section": sec.name, "lines": lines,
                "next": lines[-1]["next"] if lines else None, "first": lines[0]["addr"] if lines else None}

    def _sorted_insns(self) -> list:
        cache = getattr(self.a, "_sorted_insns", None)
        if cache is None or len(cache) != len(self.a.insns):
            cache = sorted(self.a.insns)
            self.a._sorted_insns = cache
        return cache

    def _lines_from(self, addr: int, count: int, stop: Optional[int] = None) -> list:
        s, a = self.s, self.a
        b = s.binary
        lines: list = []
        produced = 0
        while produced < count:
            if stop is not None and addr >= stop:
                break
            sec = b.section_at(addr)
            if sec is None:
                nxt = [x.addr for x in b.sections if x.addr > addr]
                if not nxt or stop is not None:
                    break
                addr = min(nxt)
                continue
            if addr == sec.addr:
                lines.append({"type": "section", "addr": hx(addr), "name": sec.name, "perms": sec.perms, "next": hx(addr)})
            fn = a.functions.get(addr)
            label = None
            if fn is not None:
                callers = a.callers(addr, through_thunks=False)
                lines.append({"type": "function", "addr": hx(addr), "name": s.name_of(addr), "kind": fn.kind,
                              "callers": [{"addr": hx(c), "name": s.name_of(c)} for c in callers[:8]], "ncallers": len(callers),
                              "next": hx(addr)})
            elif a.xrefs.count_to(addr) and (addr in s.annotations.names or b.is_code(addr)):
                label = s.name_of(addr)
            if label:
                lines.append({"type": "label", "addr": hx(addr), "name": label, "next": hx(addr)})
            if sec.executable and (fn is None or fn.kind == "normal"):
                ins = a.insns.get(addr)
                if ins is None:
                    dec = a.decode_at(addr, 1)
                    ins = dec[0] if dec else None
                if ins is None:
                    byte = b.read(addr, 1)
                    lines.append({"type": "data", "addr": hx(addr), "bytes": byte.hex(), "text": f"db 0x{byte.hex() or '00'}",
                                  "next": hx(addr + 1), "kind": "byte"})
                    addr += 1
                else:
                    ln = self._insn_line(ins)
                    ln["next"] = hx(ins.end)
                    ln["analyzed"] = ins.addr in a.insns
                    lines.append(ln)
                    addr = ins.end
                    if ins.flow in (FLOW_RET, FLOW_JMP, FLOW_IJMP):
                        lines.append({"type": "sep", "addr": hx(addr), "next": hx(addr)})
            else:
                st = a.string_at.get(addr)
                if st is not None:
                    size = st.length + (1 if st.encoding == "ascii" else 2)
                    lines.append({"type": "data", "addr": hx(addr), "kind": "string", "text": st.text[:200], "enc": st.encoding,
                                  "name": s.name_of(addr), "xrefs": self._xref_brief(addr), "next": hx(addr + size)})
                    addr += size
                elif fn is not None:  # import slot or thunk in non-exec memory
                    ps = b.ptr_size
                    lines.append({"type": "data", "addr": hx(addr), "kind": "ptr", "text": f"{'dq' if ps == 8 else 'dd'} ?",
                                  "name": s.name_of(addr), "next": hx(addr + ps), "xrefs": self._xref_brief(addr)})
                    addr += ps
                else:
                    ps = b.ptr_size
                    if addr % ps == 0 and a.xrefs.count_to(addr) or any(x.kind == "data" for x in a.xrefs.frm(addr)):
                        val = b.read_int(addr, ps) or 0
                        ptr = [x.to for x in a.xrefs.frm(addr) if x.kind == "data"]
                        lines.append({"type": "data", "addr": hx(addr), "kind": "ptr", "text": f"{'dq' if ps == 8 else 'dd'} 0x{val:x}",
                                      "target": {"addr": hx(ptr[0]), "name": s.name_of(ptr[0])} if ptr else None,
                                      "name": s.name_of(addr) if a.xrefs.count_to(addr) else None,
                                      "xrefs": self._xref_brief(addr), "next": hx(addr + ps)})
                        addr += ps
                    else:
                        n = 16 - (addr % 16)
                        nxt_str = [k for k in (a.string_at.keys()) if addr < k < addr + n]
                        if nxt_str:
                            n = min(nxt_str) - addr
                        n = min(n, sec.end - addr)
                        chunk = b.read(addr, n)
                        lines.append({"type": "data", "addr": hx(addr), "kind": "bytes", "bytes": chunk.hex(" "),
                                      "text": "db " + ", ".join(f"0x{c:02x}" for c in chunk),
                                      "name": s.name_of(addr) if a.xrefs.count_to(addr) else None,
                                      "xrefs": self._xref_brief(addr), "next": hx(addr + max(n, 1))})
                        addr += max(n, 1)
            produced += 1
        return lines

    def _xref_brief(self, addr: int) -> list:
        s, a = self.s, self.a
        out = []
        for x in a.xrefs.to(addr)[:5]:
            f = a.function_containing(x.frm)
            out.append({"addr": hx(x.frm), "kind": x.kind, "func": s.name_of(f.addr) if f else None})
        return out

    # -- details --------------------------------------------------------------
    def addrinfo(self, q) -> dict:
        s, a = self.s, self.a
        b = s.binary
        addr = parse_addr(q["addr"]) if "addr" in q else s.resolve(q.get("name", ""))
        if addr is None:
            raise ReversaError(f"'{q.get('name')}' som nenašiel.")
        fn = a.functions.get(addr)
        cont = a.function_containing(addr)
        st = a.string_at.get(addr)
        kind = ("import" if fn.kind != "normal" else "function") if fn else "string" if st else \
            ("code" if b.is_code(addr) else "data" if b.is_mapped(addr) else "unmapped")
        sec = b.section_at(addr)

        def xr(x, other):
            f = a.function_containing(other)
            return {"addr": hx(other), "kind": x.kind, "label": x.label, "func": s.name_of(f.addr) if f else None,
                    "func_addr": hx(f.addr) if f else None, "target": s.name_of(x.to)}
        info = {"addr": hx(addr), "name": s.name_of(addr), "kind": kind, "user_named": s.has_user_name(addr),
                "section": sec.name if sec else None, "offset": hx(b.addr_to_offset(addr)),
                "rva": hx(addr - b.base) if b.base else None,
                "function": {"addr": hx(cont.addr), "name": s.name_of(cont.addr)} if cont else None,
                "xrefs_to": [xr(x, x.frm) for x in a.xrefs.to(addr)][:500],
                "xrefs_from": [xr(x, x.to) for x in a.xrefs.frm(addr)][:200],
                "comment": s.annotations.comments.get(addr), "bookmark": s.annotations.bookmarks.get(addr),
                "string": st.text if st else None}
        if fn:
            info["size"] = fn.size
            info["library"] = fn.library
            info["callers"] = [{"addr": hx(c), "name": s.name_of(c)} for c in a.callers(addr)]
            info["callees"] = [{"addr": hx(c), "name": s.name_of(c)} for c in sorted(fn.callees | fn.tail_calls) if c in a.functions]
            info["blocks"] = len(fn.blocks)
            info["insns"] = fn.insn_count
        return info

    def cfg(self, q) -> dict:
        s, a = self.s, self.a
        addr = parse_addr(q["addr"])
        fn = a.functions.get(addr) or a.function_containing(addr)
        if fn is None or fn.kind != "normal":
            raise ReversaError("Graf toku riadenia (CFG) sa dá zobraziť len pre funkciu s kódom v tomto súbore.")
        g = build_cfg(fn)
        loop_nodes = {n for body in g["loops"].values() for n in body}
        nodes = []
        for n in g["nodes"][:3000]:
            blk = fn.blocks[n]
            lines = []
            for ad in blk.insns[:60]:
                ins = a.insns[ad]
                text = ins.text
                tgt = None
                for x in a.xrefs.frm(ad):
                    if x.kind in ("call", "jump"):
                        tgt = x.to
                if tgt is not None and ins.flow in (FLOW_CALL, FLOW_ICALL, FLOW_IJMP):
                    text = f"{ins.mnemonic} {s.name_of(tgt)}"
                for ref, kind in ins.refs:
                    t = s.string_text(ref)
                    if t is not None:
                        text += f'   ; "{t[:30]}"'
                        break
                lines.append({"addr": hx(ad), "text": text})
            if len(blk.insns) > 60:
                lines.append({"addr": None, "text": f"… ďalších {len(blk.insns) - 60} inštrukcií"})
            nodes.append({"id": hx(n), "lines": lines, "loop_header": n in g["loops"], "in_loop": n in loop_nodes,
                          "entry": n == g["entry"], "exit": not blk.succs})
        edges = [{"from": hx(e["from"]), "to": hx(e["to"]), "kind": e["kind"], "back": e["back"]} for e in g["edges"]]
        return {"func": hx(fn.addr), "name": s.name_of(fn.addr), "nodes": nodes, "edges": edges,
                "loops": len(g["loops"]), "truncated": len(g["nodes"]) > 3000}

    def callgraph(self, q) -> dict:
        s, a = self.s, self.a
        root = parse_addr(q["root"]) if q.get("root") else None
        depth = int(q.get("depth", 2))
        include_imports = q.get("imports", "1") == "1"
        funcs = {k: v for k, v in a.functions.items() if include_imports or v.kind == "normal"}
        g = call_graph(funcs, root, depth if root is not None else 0)
        limit = 2500
        nodes = g["nodes"][:limit]
        keep = set(nodes)
        return {"root": hx(root), "nodes": [{"id": hx(n), "name": s.name_of(n), "kind": a.functions[n].kind,
                                              "main": n == a.main} for n in nodes],
                "edges": [{"from": hx(x), "to": hx(y)} for x, y in g["edges"] if x in keep and y in keep],
                "truncated": len(g["nodes"]) > limit, "total": len(a.functions)}

    def pseudocode(self, q) -> dict:
        s = self.s
        addr = parse_addr(q["addr"])
        fn = self.a.functions.get(addr) or self.a.function_containing(addr)
        if fn is None:
            raise ReversaError(f"Na adrese 0x{addr:x} nie je žiadna funkcia.")
        lines = s.decompile(fn.addr)
        out = []
        for ln in lines:
            out.append({"indent": ln["indent"], "addr": hx(ln["addr"]),
                        "segs": [[t, k, (hx(r) if isinstance(r, int) else r)] for t, k, r in ln["segs"]]})
        return {"func": hx(fn.addr), "name": s.name_of(fn.addr), "lines": out}

    def search(self, q) -> list:
        from reversa.api.program import Program
        p = Program(self.s)
        kind, text = q.get("kind", "string"), q.get("q", "")
        regex = q.get("regex") == "1"
        if not text:
            return []
        if kind == "string":
            hits = p.search_strings(text, regex)
        elif kind == "hex":
            hits = [(x, "") for x in p.search_bytes(text)]
        elif kind == "insn":
            hits = p.search_instructions(text, regex)
        elif kind == "code":
            hits = p.search_pseudocode(text, regex)
        else:
            from reversa.analysis.search import search_text
            items = [(f.addr, self.s.name_of(f.addr)) for f in self.a.functions.values()]
            hits = search_text(items, text, regex)
        out = []
        for addr, t in hits[:1000]:
            f = self.a.function_containing(addr)
            out.append({"addr": hx(addr), "text": t[:200], "func": self.s.name_of(f.addr) if f else None})
        return out

    def view(self, q) -> dict:
        name = q.get("name", "")
        fn = self.s.views.get(name)
        if fn is None:
            raise ReversaError(f"Pohľad '{name}' neexistuje.")
        try:
            return {"name": name, "text": str(fn(self.s.plugins.api))}
        except Exception as exc:
            return {"name": name, "text": f"Pohľad zlyhal: {type(exc).__name__}: {exc}"}

    def report(self, q) -> dict:
        from reversa.report import build_report
        fmt = q.get("fmt", "md")
        return {"fmt": fmt, "text": build_report(self.s, fmt)}

    # -- mutations ------------------------------------------------------------
    def post(self, action: str, body: dict) -> dict:
        s = self.s if action not in ("open", "demo", "cancel") else None
        if action == "rename":
            new = s.rename(parse_addr(body["addr"]), body.get("name", ""))
            return {"ok": True, "name": new}
        if action == "rename_var":
            s.rename_var(parse_addr(body["func"]), body["key"], body.get("name", ""))
            return {"ok": True}
        if action == "comment":
            s.set_comment(parse_addr(body["addr"]), body.get("text"))
            return {"ok": True}
        if action == "bookmark":
            s.set_bookmark(parse_addr(body["addr"]), body.get("label"))
            return {"ok": True}
        if action == "undo":
            return {"ok": True, "what": s.undo()}
        if action == "redo":
            return {"ok": True, "what": s.redo()}
        if action == "save":
            return {"ok": True, "path": s.save(body.get("path") or None)}
        if action == "settings":
            s.settings.update(body.get("settings", {}))
            s.dirty = True
            return {"ok": True}
        if action == "open":
            path = os.path.expanduser(body.get("path", ""))
            if not os.path.isfile(path):
                raise ReversaError(f"Súbor '{path}' neexistuje.")
            self.st.open(path)
            return {"ok": True}
        if action == "demo":
            from reversa.cli import demo_path
            self.st.open(demo_path())
            return {"ok": True}
        if action == "cancel":
            self.st.cancel.set()
            return {"ok": True}
        raise ReversaError(f"Neznáma akcia '{action}'.")


GET_ROUTES = {"state", "functions", "strings", "imports", "exports", "sections", "relocations", "bookmarks", "hexdump",
              "listing", "addrinfo", "cfg", "callgraph", "pseudocode", "search", "view", "report"}


def make_handler(state: GuiState, token: str, allowed_hosts: set):
    api = Api(state)

    class Handler(BaseHTTPRequestHandler):
        server_version = "REVERSA"

        def log_message(self, fmt, *args):  # keep the terminal quiet
            pass

        def _send(self, code: int, body: bytes, ctype: str = "application/json; charset=utf-8") -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, obj, code: int = 200) -> None:
            self._send(code, json.dumps(obj, ensure_ascii=False).encode())

        def _check(self) -> bool:
            host = (self.headers.get("Host") or "").split(":")[0]
            if host not in allowed_hosts:
                self._json({"error": {"message": "Nepovolený Host."}}, 403)
                return False
            if self.path.startswith("/api/") and self.headers.get("X-Reversa-Token") != token:
                self._json({"error": {"message": "Chýba alebo nesedí prístupový token. Otvorte REVERSA cez odkaz z terminálu."}}, 403)
                return False
            return True

        def _error(self, exc: BaseException) -> None:
            if isinstance(exc, ReversaError):
                self._json({"error": {"message": exc.message, "detail": exc.detail}}, 400)
            else:
                self._json({"error": {"message": "Neočakávaná chyba v REVERSA – aplikácia beží ďalej.",
                                      "detail": "".join(traceback.format_exception(exc))}}, 500)

        def do_GET(self):  # noqa: N802
            if not self._check():
                return
            url = urllib.parse.urlparse(self.path)
            q = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
            if url.path.startswith("/api/"):
                route = url.path[5:]
                if route not in GET_ROUTES:
                    self._json({"error": {"message": "Neznáma požiadavka."}}, 404)
                    return
                try:
                    with (state.session.lock if state.session is not None else threading.RLock()):
                        self._json(getattr(api, route)(q))
                except Exception as exc:
                    self._error(exc)
                return
            if url.path.startswith("/docs/"):
                self._doc(url.path[6:])
                return
            if url.path.startswith("/i18n/"):
                self._file(I18N, url.path[6:])
                return
            self._file(STATIC, "index.html" if url.path in ("/", "") else url.path.lstrip("/"))

        def do_POST(self):  # noqa: N802
            if not self._check():
                return
            url = urllib.parse.urlparse(self.path)
            length = int(self.headers.get("Content-Length") or 0)
            try:
                if url.path == "/api/upload":
                    if length <= 0 or length > MAX_UPLOAD:
                        raise ReversaError("Súbor je prázdny alebo väčší ako 256 MB.")
                    data = self.rfile.read(length)
                    name = os.path.basename(urllib.parse.unquote(self.headers.get("X-Filename") or "upload.bin")) or "upload.bin"
                    folder = tempfile.mkdtemp(prefix="reversa-")
                    path = os.path.join(folder, name)
                    with open(path, "wb") as fh:
                        fh.write(data)
                    state.open(path, name)
                    self._json({"ok": True})
                    return
                if not url.path.startswith("/api/"):
                    self._json({"error": {"message": "Neznáma požiadavka."}}, 404)
                    return
                body = json.loads(self.rfile.read(length) or b"{}") if length else {}
                with (state.session.lock if state.session is not None else threading.RLock()):
                    self._json(api.post(url.path[5:], body))
            except Exception as exc:
                self._error(exc)

        def _file(self, root: str, rel: str) -> None:
            path = os.path.realpath(os.path.join(root, rel))
            if not path.startswith(os.path.realpath(root)) or not os.path.isfile(path):
                self._send(404, b"Not found", "text/plain")
                return
            ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
            if ctype.startswith("text/") or ctype in ("application/javascript", "application/json"):
                ctype += "; charset=utf-8"
            with open(path, "rb") as fh:
                self._send(200, fh.read(), ctype)

        def _doc(self, rel: str) -> None:
            for root in DOCS_CANDIDATES:
                p = os.path.realpath(os.path.join(root, rel))
                if p.startswith(os.path.realpath(root)) and os.path.isfile(p):
                    with open(p, "rb") as fh:
                        self._send(200, fh.read(), "text/plain; charset=utf-8")
                    return
            self._send(404, f"Dokument {rel} nie je v tejto inštalácii – nájdete ho v repozitári v priečinku docs/.".encode(),
                       "text/plain; charset=utf-8")

    return Handler


def create_server(path: Optional[str] = None, port: int = 0, options: Optional[dict] = None):
    state = GuiState(options)
    token = secrets.token_urlsafe(24)
    httpd = ThreadingHTTPServer(("127.0.0.1", port), make_handler(state, token, {"127.0.0.1", "localhost"}))
    httpd.daemon_threads = True
    if path:
        state.open(path)
    url = f"http://127.0.0.1:{httpd.server_address[1]}/?token={token}"
    return httpd, state, url, token


def run_gui(path: Optional[str] = None, port: int = 0, open_browser: bool = True, options: Optional[dict] = None) -> int:
    httpd, state, url, _ = create_server(path, port, options)
    print("REVERSA GUI beží lokálne (nič sa neposiela na internet).")
    print(f"Otvorte v prehliadači: {url}")
    print("Ukončenie: Ctrl+C")
    if open_browser:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nREVERSA ukončená.")
    finally:
        if state.session is not None:
            state.session.plugins.unload_all()
        httpd.server_close()
    return 0
