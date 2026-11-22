"""Vypíše všetky funkcie, ktoré volajú strcmp (akceptačné kritérium č. 4).

Spustenie:  python examples/scripts/strcmp_callers.py samples/bin/login_x64.elf
"""
import sys

import reversa

prog = reversa.open(sys.argv[1] if len(sys.argv) > 1 else "samples/bin/login_x64.elf")
for func in prog.callers_of("strcmp"):
    print(f"{func.name:<24} 0x{func.addr:x}")
