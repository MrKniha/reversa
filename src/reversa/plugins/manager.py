"""Minimal plugin loader.

Convention (inspired by VS Code extensions' package.json and Ghidra's
extension.properties): every plugin is a directory containing ``plugin.json``::

    {"name": "crypto-constants", "version": "1.0.0", "api_version": "1.0",
     "entry": "plugin.py", "description": "..."}

The entry module defines ``register(api)`` and may return an object (or rely
on module-level functions) with any of these hooks:

    on_load(program)                       after the binary is loaded, before analysis
    on_function_analyzed(program, func)    once per discovered function
    on_rename(program, addr, old, new)     after every rename (incl. undo/redo)
    on_unload(program)

Isolation: every hook call is wrapped; an exception disables only that plugin
and is reported as a diagnostic. NOTE: plugins are ordinary Python code running
in-process - they are *not* sandboxed. Install only plugins you trust.
"""
from __future__ import annotations

import importlib.util
import json
import os
import sys
import traceback
from dataclasses import dataclass, field
from typing import Any, Optional

from reversa.core.model import Diagnostic

API_VERSION = "1.0"
HOOKS = ("on_load", "on_function_analyzed", "on_rename", "on_unload")


@dataclass
class PluginInfo:
    name: str
    path: str
    version: str = "0"
    api_version: str = API_VERSION
    description: str = ""
    status: str = "discovered"  # discovered | active | failed | incompatible
    error: str = ""
    hooks: dict = field(default_factory=dict)
    calls: int = 0

    def as_dict(self) -> dict:
        return {"name": self.name, "version": self.version, "api_version": self.api_version, "path": self.path,
                "description": self.description, "status": self.status, "error": self.error,
                "hooks": sorted(self.hooks), "calls": self.calls}


def default_dirs() -> list[str]:
    dirs = [os.path.join(os.path.expanduser("~"), ".reversa", "plugins")]
    env = os.environ.get("REVERSA_PLUGIN_PATH")
    if env:
        dirs += [d for d in env.split(os.pathsep) if d]
    return dirs


class PluginManager:
    def __init__(self, session, dirs: Optional[list[str]] = None):
        self.session = session
        self.dirs = list(dirs) if dirs is not None else default_dirs()
        self.plugins: list[PluginInfo] = []
        self._api = None

    @property
    def api(self):
        if self._api is None:
            from reversa.api.program import Program
            self._api = Program(self.session)
        return self._api

    def discover(self) -> list[tuple[str, dict]]:
        found = []
        for d in self.dirs:
            if not os.path.isdir(d):
                continue
            for sub in sorted(os.listdir(d)):
                mpath = os.path.join(d, sub, "plugin.json")
                if os.path.isfile(mpath):
                    found.append((os.path.join(d, sub), mpath))
        return found

    def load_all(self) -> None:
        for folder, mpath in self.discover():
            self.load(folder, mpath)

    def load(self, folder: str, manifest_path: Optional[str] = None) -> PluginInfo:
        manifest_path = manifest_path or os.path.join(folder, "plugin.json")
        info = PluginInfo(os.path.basename(folder), folder)
        self.plugins.append(info)
        try:
            with open(manifest_path, encoding="utf-8") as fh:
                man = json.load(fh)
            info.name = str(man.get("name", info.name))
            info.version = str(man.get("version", "0"))
            info.api_version = str(man.get("api_version", ""))
            info.description = str(man.get("description", ""))
            if info.api_version.split(".")[0] != API_VERSION.split(".")[0]:
                info.status = "incompatible"
                info.error = f"Plugin vyžaduje API {info.api_version or '?'}, REVERSA poskytuje {API_VERSION}."
                self._report(info, info.error)
                return info
            entry = os.path.join(folder, man.get("entry", "plugin.py"))
            if not os.path.abspath(entry).startswith(os.path.abspath(folder)):
                raise ValueError("entry musí byť súbor vnútri priečinka pluginu")
            spec = importlib.util.spec_from_file_location(f"reversa_plugin_{abs(hash(folder))}", entry)
            if spec is None or spec.loader is None:
                raise ImportError(f"nedá sa načítať {entry}")
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            spec.loader.exec_module(module)
            obj: Any = module
            if hasattr(module, "register"):
                ret = module.register(self.api)
                if ret is not None:
                    obj = ret
            info.hooks = {h: getattr(obj, h) for h in HOOKS if callable(getattr(obj, h, None))}
            info.status = "active"
        except Exception as exc:  # isolation: a broken plugin never stops REVERSA
            info.status = "failed"
            info.error = f"{type(exc).__name__}: {exc}"
            self._report(info, f"Plugin '{info.name}' sa nepodarilo načítať: {info.error}", exc)
        return info

    def has_hook(self, hook: str) -> bool:
        return any(p.status == "active" and hook in p.hooks for p in self.plugins)

    def call(self, hook: str, *args) -> None:
        for p in self.plugins:
            fn = p.hooks.get(hook)
            if p.status != "active" or fn is None:
                continue
            try:
                if hook == "on_function_analyzed":
                    fn(self.api, self.api.function(args[0]))
                else:
                    fn(self.api, *args)
                p.calls += 1
            except Exception as exc:
                p.status = "failed"
                p.error = f"{hook}: {type(exc).__name__}: {exc}"
                self._report(p, f"Plugin '{p.name}' spadol v {hook} a bol vypnutý. REVERSA pokračuje ďalej.", exc)

    def unload_all(self) -> None:
        self.call("on_unload")

    def _report(self, info: PluginInfo, message: str, exc: Optional[BaseException] = None) -> None:
        detail = "".join(traceback.format_exception(exc)) if exc else ""
        self.session.runtime_diagnostics.append(Diagnostic("error", message, None, "address", detail))
