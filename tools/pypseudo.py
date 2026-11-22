"""Reference pseudocode dump from the Python implementation (same shape as rvdump --pseudo)."""
import sys
sys.path.insert(0, "src")
from reversa.session import Session
s = Session.open(sys.argv[1], load_plugins=False)
a = s.require_analysis()
for ad, f in sorted(a.functions.items()):
    if f.kind != "normal":
        continue
    print(f"== {s.name_of(ad)} {ad:x}")
    print(s.pseudocode(ad))
