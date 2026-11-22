"""Automatické anotácie + vlastný pohľad + uloženie projektu.

1. Funkcie, ktoré používajú reťazec s textom "password"/"heslo", dostanú komentár.
2. Každú funkciu volajúcu strcmp premenuje na <meno>_compares_strings (ak ešte nemá ľudské meno).
3. Zaregistruje vlastný pohľad "Volania strcmp" (zobrazí sa v GUI na karte Pohľady a v reporte).
4. Uloží projekt – po otvorení v GUI (reversa gui projekt.rvproj) sú anotácie späť.

Spustenie:  python examples/scripts/annotate_and_view.py samples/bin/login_x64_stripped.elf out.rvproj
"""
import sys

import reversa

prog = reversa.open(sys.argv[1] if len(sys.argv) > 1 else "samples/bin/login_x64_stripped.elf")

for s in prog.search_strings(r"password|heslo|flag", regex=True):
    addr, text = s
    for x in prog.xrefs_to(addr):
        prog.comment(x.frm, f"používa reťazec: {text[:40]}")

for f in prog.callers_of("strcmp"):
    if f.name.startswith("sub_"):
        f.rename(f.name + "_compares_strings")


def strcmp_view(p):
    lines = ["Funkcie volajúce strcmp:", ""]
    for f in p.callers_of("strcmp"):
        lines.append(f"  {f.name} (0x{f.addr:x}) – volaná z: {', '.join(c.name for c in f.callers) or '—'}")
    return "\n".join(lines)


prog.add_view("Volania strcmp", strcmp_view)
print(strcmp_view(prog))
if len(sys.argv) > 2:
    print("Projekt uložený:", prog.save(sys.argv[2]))
