"""Tabuľka funkcií (CSV): meno, adresa, veľkosť, počet volajúcich, volané funkcie.

Spustenie:  python examples/scripts/function_table.py samples/bin/hello_x64.elf > funkcie.csv
"""
import csv
import sys

import reversa

prog = reversa.open(sys.argv[1] if len(sys.argv) > 1 else "samples/bin/hello_x64.elf")
out = csv.writer(sys.stdout)
out.writerow(["name", "address", "size", "callers", "callees"])
for f in prog.functions():
    out.writerow([f.name, hex(f.addr), f.size, len(f.callers), " ".join(c.name for c in f.callees)])

# čítanie pamäte: prvých 16 bajtov hlavnej funkcie
if prog.main:
    print("# main bytes:", prog.read(prog.main.addr, 16).hex(" "), file=sys.stderr)
