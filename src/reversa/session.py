"""Session = one opened binary plus everything the user did with it.

This is the service layer between the headless core and every front end
(Python API, CLI, GUI server, plugins). All edits go through here so they are
undoable, published on the event bus and saved with the project.
"""
from __future__ import annotations

import os
import re
import tempfile
import threading
import traceback
from typing import Callable, Optional

from reversa.analysis.analyzer import Analysis, analyze
from reversa.core.errors import AnnotationError, LoadError
from reversa.core.events import EventBus
from reversa.core.model import Binary, Diagnostic
from reversa.core.undo import Command, UndoStack
from reversa.formats.loader import load_bytes, load_file
from reversa.project import store
from reversa.project.annotations import Annotations

NAME_RE = re.compile(r"^[A-Za-z_.$?@][A-Za-z0-9_.$?@:<>~]{0,199}$")


class Session:
    def __init__(self, binary: Binary, load_options: Optional[dict] = None, plugin_dirs: Optional[list[str]] = None,
                 load_plugins: bool = True):
        self.binary = binary
        self.load_options = load_options or {}
        self.analysis: Optional[Analysis] = None
        self.annotations = Annotations()
        self.undo_stack = UndoStack()
        self.events = EventBus(on_error=self._listener_failed)
        self.project_path: Optional[str] = None
        self.settings: dict = {}
        self.views: dict[str, Callable] = {}
        self.runtime_diagnostics: list[Diagnostic] = []
        self._decomp_cache: dict = {}
        self._symbols: dict[int, str] = {}
        self._name_index: Optional[dict[str, int]] = None
        self.lock = threading.RLock()
        self.dirty = False
        from reversa.plugins.manager import PluginManager
        self.plugins = PluginManager(self, plugin_dirs)
        if load_plugins:
            self.plugins.load_all()

    # ------------------------------------------------------------ opening
    @classmethod
    def open(cls, path: str, arch: Optional[str] = None, base: Optional[int] = None, raw: bool = False,
             plugin_dirs: Optional[list[str]] = None, load_plugins: bool = True) -> "Session":
        if store.is_project(path):
            return cls.open_project(path, plugin_dirs, load_plugins)
        binary = load_file(path, arch, base, raw)
        return cls(binary, {"arch": arch, "base": base, "raw": raw}, plugin_dirs, load_plugins)

    @classmethod
    def open_project(cls, path: str, plugin_dirs: Optional[list[str]] = None, load_plugins: bool = True) -> "Session":
        p = store.load_project(path)
        o = p["load_options"]
        binary = load_bytes(p["data"], p["path"], o.get("arch"), o.get("base"), bool(o.get("raw")))
        s = cls(binary, o, plugin_dirs, load_plugins)
        s.annotations = p["annotations"]
        s.settings = p["settings"]
        s.project_path = os.path.abspath(path)
        return s

    # ------------------------------------------------------------ analysis
    def analyze(self, progress=None, cancel: Optional[threading.Event] = None) -> Analysis:
        self.plugins.call("on_load")
        hooks = self.plugins.has_hook("on_function_analyzed")

        def on_function(fn):
            if hooks:
                self.plugins.call("on_function_analyzed", fn.addr)
        a = analyze(self.binary, progress, cancel, None)
        with self.lock:
            self.analysis = a
            self._decomp_cache.clear()
            self._symbols = {}
            for s in self.binary.symbols:
                self._symbols.setdefault(s.addr, s.name)
            for addr, n in a.auto_names.items():
                if addr not in self._symbols or n == "main":
                    self._symbols[addr] = n
            self._name_index = None
        for fn in a.functions.values():
            if fn.kind == "normal":
                on_function(fn)
        self.events.publish("analysis_done")
        return a

    def require_analysis(self) -> Analysis:
        if self.analysis is None:
            self.analyze()
        return self.analysis  # type: ignore[return-value]

    # ------------------------------------------------------------ names
    def default_name(self, addr: int) -> str:
        a = self.analysis
        if a is not None:
            if addr in a.functions:
                return f"sub_{addr:x}"
            if addr in a.string_at:
                s = a.string_at[addr].text
                slug = re.sub(r"[^A-Za-z0-9]+", "_", s[:24]).strip("_")
                return f"s_{slug}_{addr:x}" if slug else f"str_{addr:x}"
            fn = a.function_containing(addr)
            if fn is not None and fn.kind == "normal" and addr != fn.addr:
                return f"loc_{addr:x}"
        if self.binary.is_code(addr):
            return f"code_{addr:x}"
        if self.binary.is_mapped(addr):
            return f"data_{addr:x}"
        return f"0x{addr:x}"

    def name_of(self, addr: int) -> str:
        n = self.annotations.names.get(addr)
        if n:
            return n
        n = self._symbols.get(addr)
        if n:
            return n
        return self.default_name(addr)

    def has_user_name(self, addr: int) -> bool:
        return addr in self.annotations.names

    def resolve(self, ref) -> Optional[int]:
        """Address from an int, '0x401000', a name, or an import name ('strcmp', '_strcmp', 'strcmp@plt')."""
        if isinstance(ref, int):
            return ref
        text = str(ref).strip()
        idx = self._names()
        for cand in (text, "_" + text, text.split("@")[0], "__imp_" + text):
            if cand in idx:
                return idx[cand]
        m = re.fullmatch(r"(?:sub|loc|data|str|s_\w*?)_([0-9a-fA-F]+)", text)
        if m:  # default names like sub_401196 are not indexed; decode the address
            text = m.group(1)
        try:
            v = int(text, 16)
        except ValueError:
            return None
        if self.binary.is_mapped(v) or (self.analysis is not None and v in self.analysis.functions):
            return v
        return None

    def _names(self) -> dict[str, int]:
        if self._name_index is None:
            idx: dict[str, int] = {}
            if self.analysis:
                for name, addr in self.analysis.import_addr.items():
                    idx.setdefault(name, addr)
            for addr, n in self._symbols.items():
                idx.setdefault(n, addr)
            for addr, n in self.annotations.names.items():
                idx[n] = addr
            self._name_index = idx
        return self._name_index

    # ------------------------------------------------------------ edits (undoable)
    def _edit(self, description: str, do: Callable[[], None], undo: Callable[[], None]) -> None:
        with self.lock:
            self.undo_stack.push(Command(description, do, undo))
            self.dirty = True

    def rename(self, ref, new_name: str) -> str:
        addr = self.resolve(ref)
        if addr is None:
            raise AnnotationError(f"Adresu alebo meno '{ref}' som nenašiel.")
        if not self.binary.is_mapped(addr) and addr not in self._names().values():
            raise AnnotationError(f"Adresa 0x{addr:x} neleží v žiadnej sekcii súboru.")
        new_name = (new_name or "").strip()
        if new_name and not NAME_RE.match(new_name):
            raise AnnotationError("Meno môže obsahovať písmená, číslice a _ . $ @ : (bez medzier, max. 200 znakov) "
                                  "a nesmie začínať číslicou.")
        other = self._names().get(new_name)
        if new_name and other is not None and other != addr:
            raise AnnotationError(f"Meno '{new_name}' už používa adresa 0x{other:x}. Zvoľte iné meno.")
        old_user = self.annotations.names.get(addr)
        old_display = self.name_of(addr)

        def apply(value):
            def f():
                if value:
                    self.annotations.names[addr] = value
                else:
                    self.annotations.names.pop(addr, None)
                self._name_index = None
                self.events.publish("renamed", addr=addr, old=old_display, new=self.name_of(addr))
                self.plugins.call("on_rename", addr, old_display, self.name_of(addr))
            return f
        self._edit(f"Premenovanie 0x{addr:x} na {new_name or '(pôvodné)'}", apply(new_name), apply(old_user))
        return self.name_of(addr)

    def rename_var(self, func, key: str, new_name: str) -> None:
        faddr = self.resolve(func)
        if faddr is None:
            raise AnnotationError(f"Funkciu '{func}' som nenašiel.")
        new_name = (new_name or "").strip()
        if new_name and not NAME_RE.match(new_name):
            raise AnnotationError("Neplatné meno premennej (písmená, číslice, _; nesmie začínať číslicou).")
        k = (faddr, key)
        old = self.annotations.var_names.get(k)

        def apply(value):
            def f():
                if value:
                    self.annotations.var_names[k] = value
                else:
                    self.annotations.var_names.pop(k, None)
                self.events.publish("var_renamed", func=faddr, key=key, new=value)
            return f
        self._edit(f"Premenovanie premennej {key}", apply(new_name), apply(old))

    def _set_map(self, mapping: dict, topic: str, label: str, ref, value: Optional[str]) -> None:
        addr = self.resolve(ref)
        if addr is None:
            raise AnnotationError(f"Adresu '{ref}' som nenašiel.")
        old = mapping.get(addr)
        value = (value or "").strip() or None

        def apply(v):
            def f():
                if v:
                    mapping[addr] = v
                else:
                    mapping.pop(addr, None)
                self.events.publish(topic, addr=addr, value=v)
            return f
        self._edit(f"{label} 0x{addr:x}", apply(value), apply(old))

    def set_comment(self, ref, text: Optional[str]) -> None:
        self._set_map(self.annotations.comments, "comment", "Komentár", ref, text)

    def set_bookmark(self, ref, label: Optional[str]) -> None:
        self._set_map(self.annotations.bookmarks, "bookmark", "Záložka", ref, label)

    def undo(self) -> Optional[str]:
        with self.lock:
            d = self.undo_stack.undo()
            if d:
                self.dirty = True
            return d

    def redo(self) -> Optional[str]:
        with self.lock:
            d = self.undo_stack.redo()
            if d:
                self.dirty = True
            return d

    # ------------------------------------------------------------ pseudocode
    def decompile(self, func) -> list[dict]:
        from reversa.decompiler import decompile, render
        a = self.require_analysis()
        faddr = self.resolve(func)
        if faddr is None or faddr not in a.functions:
            raise AnnotationError(f"'{func}' nie je funkcia.")
        fn = a.functions[faddr]
        if fn.kind != "normal":
            lib = f" z knižnice {fn.library}" if fn.library else ""
            return [{"indent": 0, "segs": [(f"// {self.name_of(faddr)} je importovaná funkcia{lib} – jej kód nie je v tomto súbore.", "comment", None)], "addr": faddr}]
        d = self._decomp_cache.get(faddr)
        if d is None:
            try:
                from reversa.decompiler import decompile as _dec
                d = _dec(a, faddr)
            except Exception as exc:  # report instead of crashing the UI
                return [{"indent": 0, "segs": [(f"// Pseudokód sa nepodarilo vytvoriť: {type(exc).__name__}: {exc}", "comment", None)], "addr": faddr},
                        {"indent": 0, "segs": [("// " + line, "comment", None) for line in traceback.format_exc().splitlines()[-3:]], "addr": None}]
            self._decomp_cache[faddr] = d
        return render(d, self.name_of(faddr), self.name_of,
                      lambda key: self.annotations.var_names.get((faddr, key)), self.string_text, self.is_symbol)

    def pseudocode(self, func) -> str:
        from reversa.decompiler import to_text
        return to_text(self.decompile(func))

    def string_text(self, addr: int) -> Optional[str]:
        a = self.analysis
        if a and addr in a.string_at:
            return a.string_at[addr].text
        return None

    def is_symbol(self, v: int) -> bool:
        a = self.analysis
        if a is None or v < 0x100:
            return False
        if v in a.string_at or v in a.functions or v in self.annotations.names:
            return True
        if v >= 0x1000:
            s = self.binary.section_at(v)
            return bool(s and not s.executable)
        return False

    # ------------------------------------------------------------ persistence
    def save(self, path: Optional[str] = None) -> str:
        path = path or self.project_path or default_project_path(self.binary.path)
        store.save_project(path, self.binary.data, self.binary.path, self.load_options, self.annotations, self.settings)
        self.project_path = os.path.abspath(path)
        self.dirty = False
        self.events.publish("saved", path=self.project_path)
        return self.project_path

    # ------------------------------------------------------------ misc
    @property
    def diagnostics(self) -> list[Diagnostic]:
        return list(self.binary.diagnostics) + self.runtime_diagnostics

    def _listener_failed(self, topic, handler, exc) -> None:
        self.runtime_diagnostics.append(Diagnostic("error", f"Poslucháč udalosti '{topic}' zlyhal: {exc}", None, "address",
                                                   "".join(traceback.format_exception(exc))))

    def add_view(self, name: str, render: Callable) -> None:
        self.views[name] = render


def default_project_path(binary_path: str) -> str:
    base = os.path.basename(binary_path) or "projekt"
    folder = os.path.dirname(os.path.abspath(binary_path)) if not binary_path.startswith("<") else ""
    package_dir = os.path.dirname(os.path.abspath(__file__))
    temp_dir = os.path.realpath(tempfile.gettempdir())
    if (not folder or not os.access(folder, os.W_OK) or os.path.realpath(folder).startswith(package_dir)
            or (os.path.realpath(folder).startswith(temp_dir) and os.path.basename(folder).startswith("reversa-"))):
        folder = os.path.join(os.path.expanduser("~"), "reversa-projects")
        os.makedirs(folder, exist_ok=True)
    return os.path.join(folder, base + ".rvproj")
