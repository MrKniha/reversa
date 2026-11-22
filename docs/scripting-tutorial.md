# Tutoriál: skriptovanie v Pythone

REVERSA je Python knižnica. Všetko, čo vidíte v GUI (funkcie, x-refs, pseudokód,
premenovania), máte k dispozícii aj zo skriptu. API je stabilné vo verzii `1.0`
(rovnaké API dostávajú pluginy).

## Ako skript spustiť

```bash
# 1) ako bežný Python skript (REVERSA musí byť nainštalovaná)
python moj_skript.py samples/bin/login_x64.elf

# 2) cez REVERSA (rovnaký Python, v ktorom je REVERSA nainštalovaná);
#    argumenty za skriptom dostane skript v sys.argv
reversa script moj_skript.py samples/bin/login_x64.elf
```

## Prvý skript: kto volá strcmp (akceptačné kritérium: ≤ 10 riadkov)

```python
import sys
import reversa

prog = reversa.open(sys.argv[1])
for func in prog.callers_of("strcmp"):
    print(f"{func.name:<24} 0x{func.addr:x}")
```

Výstup pre `samples/bin/login_x64.elf`:

```
check_password           0x401196
check_user               0x401217
```

Súbor je v [examples/scripts/strcmp_callers.py](../examples/scripts/strcmp_callers.py).
Funguje aj na Windows PE (`login_x64.exe`, import `strcmp` z msvcrt.dll), na ARM64
a na binárkach bez symbolov.

## Prehľad API

`reversa.open(cesta)` vráti objekt `Program` (súbor je už analyzovaný).
Kdekoľvek API čaká adresu, môžete zadať číslo, text `"0x401196"` alebo meno
(`"main"`, `"strcmp"`, `"sub_401196"`).

| Volanie | Čo vráti / urobí |
|---|---|
| `prog.path`, `prog.arch`, `prog.format`, `prog.entry` | základné informácie |
| `prog.main` | funkcia `main` (alebo `None`) |
| `prog.functions(include_imports=False)` | zoznam funkcií (`FunctionView`) |
| `prog.function(ref)` | jedna funkcia |
| `prog.callers_of(ref)` | funkcie, ktoré volajú `ref` (aj cez PLT/IAT) |
| `prog.xrefs_to(ref)`, `prog.xrefs_from(ref)` | krížové odkazy (`frm`, `to`, `kind`) |
| `prog.imports()`, `prog.exports()`, `prog.sections()` | tabuľky súboru |
| `prog.strings(min_len=4)` | nájdené reťazce |
| `prog.search_strings(text, regex=False)` | dvojice `(adresa, text)` |
| `prog.search_bytes("48 8b ?? 05")` | adresy, kde sa vzor nachádza (`??` = ľubovoľný bajt) |
| `prog.read(addr, size)`, `prog.read_string(addr)` | čítanie pamäte súboru |
| `prog.disassemble(addr, count)` | inštrukcie (`addr`, `mnemonic`, `op_str`) |
| `prog.decompile(ref)` | pseudokód ako text |
| `prog.rename(ref, meno)`, `prog.rename_variable(func, kľúč, meno)` | premenovanie (s undo) |
| `prog.comment(ref, text)`, `prog.bookmark(ref, popis)` | anotácie (s undo) |
| `prog.undo()`, `prog.redo()` | vrátenie a zopakovanie zmien |
| `prog.add_view(názov, funkcia)` | vlastný textový pohľad (GUI karta *Pohľady*, report) |
| `prog.save(cesta.rvproj)` | uloženie projektu |

`FunctionView` má `name`, `addr`, `size`, `kind`, `is_import`, `callers`,
`callees`, `xrefs`, `instructions()`, `pseudocode()` a `rename(meno)`.

## Príklad: tabuľka funkcií do CSV

```python
import csv, sys
import reversa

prog = reversa.open(sys.argv[1])
out = csv.writer(sys.stdout)
out.writerow(["name", "address", "size", "callers", "callees"])
for f in prog.functions():
    out.writerow([f.name, hex(f.addr), f.size, len(f.callers), " ".join(c.name for c in f.callees)])
```

Celý príklad: [examples/scripts/function_table.py](../examples/scripts/function_table.py).

## Príklad: automatické anotácie a vlastný pohľad

[examples/scripts/annotate_and_view.py](../examples/scripts/annotate_and_view.py)
komentuje miesta, ktoré používajú reťazce s „password“, premenuje volajúcich
`strcmp` a uloží projekt. Výsledok otvoríte v GUI:

```bash
python examples/scripts/annotate_and_view.py samples/bin/login_x64_stripped.elf out.rvproj
reversa gui out.rvproj
```

## Tipy

- Premenovanie zo skriptu sa okamžite prejaví v pseudokóde (`prog.decompile`)
  aj v x-refs. Po uložení ho uvidíte v GUI.
- Neznáme meno alebo adresa vyvolá `reversa.core.errors.ReversaError` so
  zrozumiteľnou správou.
- Pseudokód je **dočasný** (MVP): typy sú odhadnuté. Dôležité závery si overte
  v listingu.
- Skripty bežia ako bežný Python bez sandboxu. Spúšťajte len skripty, ktorým
  dôverujete.
