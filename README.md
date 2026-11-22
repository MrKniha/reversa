# REVERSA

**Open-source nástroj na reverzné inžinierstvo pre začiatočníkov aj profesionálov.**
Disassembler, pseudokód (dekompilátor), krížové odkazy (x-refs), grafy funkcií
a volaní, Python API, pluginy a grafické rozhranie, ktoré beží lokálne vo vašom
prehliadači.

- Formáty: ELF, PE (32/64-bit), surové dáta (firmvér)
- Architektúry: x86, x86-64, AArch64 (ARM64), ARM 32-bit (len disassembly)
- Statická analýza: súbor sa **nikdy nespúšťa**. Bez cloudu, bez telemetrie.
- Licencia: MIT

> Verzia 0.1.0 je MVP. Pseudokód je **dočasný**: typy sú odhadnuté. Dôležité
> závery si overte v listingu. Známe obmedzenia sú v
> [docs/architecture.md](docs/architecture.md#známe-obmedzenia-010).

## Rýchly štart

```bash
pip install .            # z priečinka repozitára
reversa --demo           # otvorí GUI s ukážkovou analýzou
```

Bez grafického prostredia: `reversa --demo --no-gui`.

```bash
reversa gui program.exe                                   # GUI s vlastným súborom
reversa analyze program.elf --report report.md            # headless report
reversa analyze program.elf --pseudocode main             # pseudokód jednej funkcie
reversa search program.elf --insn "call.*strcmp"          # hľadanie v inštrukciách
reversa script examples/scripts/strcmp_callers.py program.elf
reversa gui firmware.bin --arch arm64 --base 0x8000000    # surové dáta
```

## Prvá analýza za 10 minút

1. `reversa --demo`. Otvorí sa ukážkový program `login`, ktorý pýta meno a heslo.
2. Vľavo kliknite na **★ main**, v strede na kartu **Pseudokód** (kláves `2`).
   `main` volá `check_user` a `check_password`.
3. Na karte **Reťazce** kliknite na `Wrong password.`. Panel **Detaily** vpravo
   ukáže, kto reťazec používa (x-refs).
4. Na karte **Importy** kliknite na `strcmp`. Volajú ho `check_user`
   a `check_password`. V ich pseudokóde nájdete meno aj heslo.
5. Premenujte funkciu (kláves `N`), vráťte zmenu (`Ctrl+Z`) a uložte projekt
   (`Ctrl+S`).

Celý postup s riešením: [docs/quickstart.md](docs/quickstart.md) a
[docs/tutorials.md](docs/tutorials.md).

## Inštalácia

Potrebujete **Python 3.10 alebo novší** a moderný prehliadač (Firefox, Chrome,
Edge, Safari). Jediná závislosť je Capstone, ktorý pip nainštaluje automaticky.

### Linux

```bash
sudo apt install python3 python3-venv git      # Debian/Ubuntu (Fedora: dnf install python3 git)
git clone <URL repozitára> reversa && cd reversa
python3 -m venv .venv && . .venv/bin/activate
pip install .
reversa --demo
```

### Windows

1. Nainštalujte Python z https://www.python.org/downloads/ (zaškrtnite
   „Add python.exe to PATH").
2. V PowerShell:
   ```powershell
   git clone <URL repozitára> reversa; cd reversa
   py -m venv .venv; .venv\Scripts\Activate.ps1
   pip install .
   reversa --demo
   ```

### macOS

```bash
brew install python git          # alebo Python z python.org
git clone <URL repozitára> reversa && cd reversa
python3 -m venv .venv && . .venv/bin/activate
pip install .
reversa --demo
```

Inštalácia z wheel balíka: `pip install reversa-0.1.0-py3-none-any.whl`.
Na vývoj: `pip install -e ".[dev]"` (pozri [CONTRIBUTING.md](CONTRIBUTING.md)).

## Bezpečná analýza malvéru

REVERSA robí iba statickú analýzu. Súbor číta, ale nespúšťa. Napriek tomu
pri práci s podozrivými súbormi dodržujte tieto pravidlá:

1. **Pracujte vo virtuálnom stroji** (VirtualBox, VMware, QEMU/KVM), nie na
   svojom bežnom počítači. Pred analýzou si spravte snapshot.
2. **Odpojte sieť** virtuálneho stroja alebo použite izolovanú sieť
   (host-only). Malvér môže kontaktovať riadiace servery.
3. **Nikdy súbor nespúšťajte** mimo izolovaného prostredia. Ani „len na skúšku".
4. **Premenujte súbory** tak, aby sa nedali omylom spustiť (napr.
   `vzorka.exe_` alebo `vzorka.bin`). Archívy so vzorkami chráňte heslom
   (konvencia `infected`).
5. **Nezdieľajte priečinky** medzi hostiteľom a VM, kým to nepotrebujete.
   Vypnite automatické otváranie súborov a náhľady.
6. **Pozor na skripty a pluginy.** Bežia bez sandboxu. Nepoužívajte pluginy
   z neznámych zdrojov.
7. **Overte si hash** (SHA-256) vzorky pred a po analýze. REVERSA ho ukazuje
   v reporte, v tooltipe názvu súboru v stavovom riadku GUI a ukladá ho do projektu.
8. **Poškodené súbory sú v poriadku.** REVERSA načíta, čo sa dá, a ohlási
   chyby v diagnostike (*Pokročilé → Diagnostika načítania*). Nespadne.
9. **Dodržujte zákony a pravidlá organizácie.** Vzorky malvéru neposielajte
   ďalej mimo dôveryhodných kanálov (napr. CSIRT, výrobca antivírusu).

Do tohto repozitára nikdy nepridávajte malvér ani binárky bez zdrojového kódu.
Ukážky v `samples/` sú vlastné malé programy so zdrojmi v `samples/src/`.

## Etika

REVERSA slúži na učenie, CTF súťaže, analýzu malvéru, bezpečnostný výskum,
interoperabilitu a ladenie vlastného softvéru.

**REVERSA neobsahuje a nikdy nebude obsahovať funkcie na obchádzanie DRM,
licenčných kontrol, aktivácie, anti-cheat systémov alebo ochrany proti
kopírovaniu.** Takéto príspevky neprijmeme.

**Hranica patch módu:** budúci patch mód (roadmapa, nie je v 0.1.0) bude určený
na úpravy **vlastných** programov alebo programov, na ktoré máte povolenie:
opravy chýb, výučba, CTF úlohy. Nebude obsahovať automatické „crackovanie",
hľadanie a odstraňovanie licenčných kontrol ani podobné funkcie. Za to, ako
nástroj použijete, zodpovedáte vy. Analyzujte iba softvér, na ktorý máte právo.

## Dokumentácia

| Dokument | Obsah |
|---|---|
| [docs/quickstart.md](docs/quickstart.md) | Rýchly štart za 10 minút |
| [docs/tutorials.md](docs/tutorials.md) | Tutoriály: prvá analýza, nájdite `main`, pochopte slučku, iné architektúry |
| [docs/scripting-tutorial.md](docs/scripting-tutorial.md) | Python API a skripty |
| [docs/plugin-tutorial.md](docs/plugin-tutorial.md) | Ako napísať plugin |
| [docs/glossary.md](docs/glossary.md) | Slovník pojmov |
| [docs/architecture.md](docs/architecture.md) | Architektúra, rozhodnutia, roadmapa |
| [docs/research.md](docs/research.md) | Výskum, porovnania, záznam REA |
| [docs/status.md](docs/status.md) | Stav projektu a akceptačné kritériá |

## Licencia

REVERSA je pod licenciou **MIT**, pozri [LICENSE](LICENSE). Licencie
komponentov tretích strán sú v [LICENSE-THIRD-PARTY](LICENSE-THIRD-PARTY).
Skript `scripts/check_licenses.py` ich kontroluje v CI.

## Poďakovanie

Ďakujeme autorom projektov, z ktorých sme sa učili: Ghidra, radare2, Rizin,
Cutter, angr, Capstone, Detect It Easy, RetDec, LIEF, pyelftools, pefile,
binwalk a dagre. Zvlášť ďakujeme tímu **Capstone**, bez ktorého by REVERSA
nevedela čítať strojový kód. radare2 nám slúžil na nezávislú kontrolu výsledkov.

## Výskum a referencie

Podrobnosti, hviezdičky, dátumy a záznam použitia REA sú v
[docs/research.md](docs/research.md). Žiadny kód z týchto projektov nie je
v REVERSA skopírovaný.

| Projekt | URL | Licencia | Čo sme prebrali |
|---|---|---|---|
| Ghidra | https://github.com/NationalSecurityAgency/ghidra | Apache-2.0 | IR medzi assemblerom a pseudokódom, rozdelenie pohľadov, manifest rozšírení |
| radare2 | https://github.com/radareorg/radare2 | LGPL-3.0 | Krátke skratky. Nezávislá kontrola výsledkov |
| Rizin | https://github.com/rizinorg/rizin | LGPL-3.0 | Projekt ako súbor s verziou schémy |
| Cutter | https://github.com/rizinorg/cutter | GPL-3.0 | Rozloženie GUI, synchronizácia pohľadov |
| angr | https://github.com/angr/angr | BSD-2-Clause | Rekurzívny zostup, noreturn funkcie, Python API |
| Capstone | https://github.com/capstone-engine/capstone | BSD-3-Clause | **Závislosť**: disassembler |
| Detect It Easy | https://github.com/horsicq/Detect-It-Easy | MIT | Detekcia kompilátora a packera |
| RetDec | https://github.com/avast/retdec | MIT | Fázy dekompilácie, označenie výstupu ako odhad |
| LIEF | https://github.com/lief-project/LIEF | Apache-2.0 | Jednotný model binárky |
| pyelftools | https://github.com/eliben/pyelftools | Unlicense | Porovnanie s vlastným ELF parserom |
| pefile | https://github.com/erocarrera/pefile | MIT | Porovnanie s vlastným PE parserom |
| binwalk | https://github.com/ReFirmLabs/binwalk | MIT | Surové dáta s ručnou architektúrou a bázou |
| dagre | https://github.com/dagrejs/dagre | MIT | Algoritmus vrstveného rozloženia grafov |
| Unicorn, Keystone | https://github.com/unicorn-engine/unicorn, https://github.com/keystone-engine/keystone | GPL-2.0 | Nič (len roadmapa, voliteľne) |
| Zydis, iced | https://github.com/zyantific/zydis, https://github.com/icedland/iced | MIT | Nič (zvažované alternatívy k Capstone) |
| Frida, pwndbg | https://github.com/frida/frida, https://github.com/pwndbg/pwndbg | wxWindows / MIT | Nič (dynamická analýza je mimo MVP) |
| REA | `npx rea-agents` | — | Inšpekcia artefaktov. Plná analýza nebola možná bez Ghidry |
