"""Reference dump from the Python implementation (same shape as tools/rvdump.cpp)."""
import json, sys
sys.path.insert(0, "src")
from reversa.formats.loader import load_file
from reversa.analysis.analyzer import analyze
b = load_file(sys.argv[1]); a = analyze(b)
cs = a.import_addr.get("strcmp", a.import_addr.get("_strcmp"))
print(json.dumps({"format": b.format, "arch": b.arch, "entry": b.entry,
  "sections": [[s.name, s.addr, s.size, s.offset, s.file_size, s.perms] for s in b.sections],
  "symbols": len(b.symbols), "imports": [[i.name, i.slot] for i in b.imports], "relocs": len(b.relocations),
  "compiler": b.compiler, "main": a.main,
  "functions": [[ad, f.kind, f.size, f.insn_count, len(f.blocks)] for ad, f in sorted(a.functions.items())],
  "xrefs": len(a.xrefs), "strings": len(a.strings), "callers_strcmp": a.callers(cs) if cs is not None else []}, separators=(",", ":"), ensure_ascii=False))
