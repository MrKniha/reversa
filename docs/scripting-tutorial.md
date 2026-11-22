# Tutoriál: skripty v C++

Skript je malá zdieľaná knižnica (`.so` na Linuxe, `.dll` na Windows), ktorú
spustí príkaz `reversa script`. Všetko, čo vidíte v GUI (funkcie, x-refs,
pseudokód, premenovania), máte k dispozícii aj zo skriptu. Skripty aj pluginy
používajú rovnaké stabilné rozhranie v jazyku C, verzia `RV_API_VERSION 1`
([sdk/include/reversa/plugin.h](../sdk/include/reversa/plugin.h)).
Pre C++ je k nemu tenký pomocník bez ďalších závislostí
([sdk/include/reversa/sdk.hpp](../sdk/include/reversa/sdk.hpp)).

## Prvý skript: kto volá strcmp

```cpp
#include <reversa/sdk.hpp>

#include <cstdio>

int run(const rv_host* host, const std::vector<std::string>& args) {
    auto prog = reversa::Program::open(host, args.size() > 1 ? args[1] : "samples/bin/login_x64.elf");
    for (auto& f : prog.callers_of("strcmp")) std::printf("%-24s %s\n", f.name.c_str(), reversa::hex(f.addr).c_str());
    return 0;
}
REVERSA_SCRIPT(run)
```

Súbor je v [examples/scripts/strcmp_callers.cpp](../examples/scripts/strcmp_callers.cpp).
`args[0]` je cesta ku skriptu, ďalšie prvky sú argumenty za skriptom. Návratová
hodnota `run` je návratový kód príkazu `reversa script`. Výnimka (napríklad
neznáme meno funkcie) sa vypíše ako chyba a skript skončí s kódom 1.

## Preklad a spustenie

Hlavičky SDK sú v balíku REVERSA: Linux `/usr/include/reversa`, Windows
`bin\sdk\include\reversa` v rozbalenom zip-e. Netreba linkovať žiadnu knižnicu.

```bash
# Linux (g++ alebo clang++)
g++ -std=c++17 -shared -fPIC -o strcmp_callers.so strcmp_callers.cpp
reversa script ./strcmp_callers.so samples/bin/login_x64.elf
```

```bat
:: Windows (Developer Command Prompt pre Visual Studio)
cl /std:c++17 /EHsc /LD /I C:\reversa\bin\sdk\include strcmp_callers.cpp
reversa script strcmp_callers.dll samples\bin\login_x64.exe
```

Výstup pre `samples/bin/login_x64.elf`:

```
check_password           0x401196
check_user               0x401217
```

Skript funguje aj na Windows PE (`login_x64.exe`, import `strcmp` z msvcrt.dll),
na ARM64 a na binárkach bez symbolov. Preložené príklady sú v balíku: Linux
`/usr/lib/reversa/examples/scripts/`, Windows `bin\examples\scripts\`.
Pri vývoji REVERSA ich CMake preloží do `build/bin/examples/scripts/`.

## Prehľad API (C++)

`reversa::Program::open(host, cesta)` otvorí binárku alebo projekt `.rvproj`
a analyzuje ho. Program sa zatvorí, keď objekt zanikne. Kdekoľvek API čaká meno,
môžete zadať aj text `"0x401196"`, `"main"`, `"strcmp"` alebo `"sub_401196"`.

| Volanie | Čo vráti / urobí |
|---|---|
| `path()`, `format()`, `arch()`, `entry()` | základné informácie |
| `main()` | funkcia `main` (`std::optional<Function>`) |
| `functions(include_imports = false)` | zoznam funkcií |
| `function(ref)` | jedna funkcia (`addr`, `name`, `size`, `insn_count`, `block_count`, `is_import`) |
| `resolve(ref)`, `addr(ref)`, `name_of(addr)` | mená a adresy (`addr` pri neznámom mene vyhodí `reversa::Error`) |
| `callers_of(ref)`, `callees_of(addr)` | kto funkciu volá a koho volá ona (aj cez PLT/IAT) |
| `xrefs_to(addr)` | krížové odkazy (`from`, `to`, `kind`) |
| `strings()` | nájdené reťazce (`addr`, `text`) |
| `search_bytes("48 8b ?? 05")` | adresy, kde sa vzor nachádza (`??` = ľubovoľný bajt) |
| `read(addr, size)` | bajty zo súboru |
| `pseudocode(addr)` | pseudokód ako text |
| `rename(addr, meno)`, `set_comment(addr, text)`, `set_bookmark(addr, popis)` | anotácie (s undo); prázdny text ich zmaže |
| `comment(addr)`, `bookmark(addr)` | prečítanie anotácií |
| `add_view(názov, funkcia)` | vlastný textový pohľad (GUI karta *Pohľady*, report) |
| `save(cesta.rvproj)` | uloženie projektu (bez cesty vedľa binárky) |
| `log(text)` | výpis na štandardný výstup |

## Príklad: tabuľka funkcií do CSV

[examples/scripts/function_table.cpp](../examples/scripts/function_table.cpp)
vypíše meno, adresu, veľkosť, počet volajúcich a volané funkcie:

```bash
reversa script function_table.so samples/bin/hello_x64.elf > functions.csv
```

## Príklad: automatické anotácie a uložený projekt

[examples/scripts/annotate_and_view.cpp](../examples/scripts/annotate_and_view.cpp)
okomentuje miesta, ktoré používajú reťazce s „password“, premenuje volajúcich
`strcmp` a uloží projekt. Výsledok otvoríte v GUI:

```bash
reversa script annotate_and_view.so samples/bin/login_x64_stripped.elf out.rvproj
reversa gui out.rvproj
```

## Skript v čistom C

`sdk.hpp` je len pohodlie. Skript môže použiť priamo tabuľku funkcií `rv_host`:

```c
#include <stdio.h>
#include <reversa/plugin.h>

RV_EXPORT int reversa_script(const rv_host* host, int argc, const char* const* argv) {
    rv_program* p = host->open(argc > 1 ? argv[1] : "samples/bin/login_x64.elf");
    if (!p) { fprintf(stderr, "%s\n", host->last_error()); return 1; }
    printf("%s / %s, %zu functions\n", host->format(p), host->arch(p), host->function_count(p));
    host->close(p);
    return 0;
}
```

## Tipy

- Premenovanie zo skriptu sa okamžite prejaví v pseudokóde aj v x-refs. Po
  uložení projektu ho uvidíte v GUI.
- Text, ktorý vráti funkcia `rv_host`, platí len do ďalšieho volania na tom istom
  vlákne. `sdk.hpp` ho kopíruje do `std::string` za vás.
- Pseudokód je **dočasný** (MVP): typy sú odhadnuté. Dôležité závery si overte
  v listingu.
- Skript beží v procese REVERSA bez sandboxu. Spúšťajte len skripty, ktorým
  dôverujete.
- Python skripty (`.py`) z verzie 0.1 už REVERSA nespúšťa. Prepíšte ich podľa
  tohto návodu.
