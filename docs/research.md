# Výskum a referencie

Tento dokument zaznamenáva, z čoho sme pri návrhu REVERSA vychádzali, aké nástroje
sme použili pri výskume a čo z nich (ne)fungovalo. Údaje o repozitároch sme zistili
22. 11. 2026 cez GitHub API (`gh api repos/<owner>/<repo>`). Hviezdičky a dátum
posledného pushu sa menia, berte ich ako orientačné.

**Žiadny kód z týchto projektov nie je v REVERSA skopírovaný.** Prevzali sme len
myšlienky, konvencie a overené postupy (pozri stĺpec „čo sme prebrali"). Jedinou
priamou závislosťou je Capstone (BSD-3-Clause). Pozri [LICENSE-THIRD-PARTY](../LICENSE-THIRD-PARTY).

## Projekty

| Projekt | URL | Licencia (podľa GitHub) | ★ | posledný push | Čo sme prebrali |
|---|---|---|---|---|---|
| Ghidra | https://github.com/NationalSecurityAgency/ghidra | Apache-2.0 | 100 942 | 2026-11-20 | Rozdelenie na listing / dekompilátor / graf. Pomenovanie `FUN_`/`DAT_` sme upravili na `sub_`/`s_`. Myšlienka P-code: medzijazyk (IR) medzi assemblerom a pseudokódom. Manifest rozšírení (`extension.properties`) ako vzor pre `plugin.json`. |
| radare2 | https://github.com/radareorg/radare2 | LGPL-3.0 (GitHub: NOASSERTION) | 25 483 | 2026-11-22 | Krátke klávesové skratky a príkazy. Použili sme ho ako **nezávislú kontrolu** výsledkov (pozri nižšie). |
| Rizin | https://github.com/rizinorg/rizin | LGPL-3.0 | 4 220 | 2026-11-22 | Projekty ako samostatný súbor s verziou schémy. |
| Cutter | https://github.com/rizinorg/cutter | GPL-3.0 | 20 250 | 2026-11-13 | Rozloženie GUI: funkcie vľavo, karty v strede, detaily vpravo. Synchronizácia pohľadov podľa jednej „aktuálnej adresy". |
| angr | https://github.com/angr/angr | BSD-2-Clause | 9 406 | 2026-11-20 | Rekurzívny zostup pri hľadaní funkcií, rozpoznanie noreturn funkcií, štruktúra CFG. Python API ako hlavné rozhranie. |
| Capstone | https://github.com/capstone-engine/capstone | BSD-3-Clause | 9 204 | 2026-11-21 | **Závislosť**: disassembler pre x86, x86-64, ARM, AArch64. |
| Unicorn | https://github.com/unicorn-engine/unicorn | GPL-2.0 | 9 601 | 2026-11-10 | Nepoužité v MVP (emulácia je v roadmape). Kvôli GPL-2.0 by bola len voliteľný plugin. |
| Frida | https://github.com/frida/frida | wxWindows Library Licence (GitHub: NOASSERTION) | 22 892 | 2026-11-20 | Nepoužité. Dynamická analýza je mimo MVP. |
| pwndbg | https://github.com/pwndbg/pwndbg | MIT | 11 238 | 2026-11-19 | Prehľadné zobrazenie kontextu (registre, zásobník) ako inšpirácia pre budúci debugger pohľad. |
| Detect It Easy | https://github.com/horsicq/Detect-It-Easy | MIT | 11 991 | 2026-11-22 | Detekcia kompilátora a packerov (UPX) podľa sekcií a reťazcov. |
| binwalk | https://github.com/ReFirmLabs/binwalk | MIT | 14 734 | 2026-08-11 | Surové dáta (firmvér) ako samostatný formát s ručnou architektúrou a bázou. |
| LIEF | https://github.com/lief-project/LIEF | Apache-2.0 | 5 678 | 2026-11-17 | Jednotný model binárky (sekcie, symboly, importy, exporty, relokácie) pre ELF aj PE. |
| RetDec | https://github.com/avast/retdec | MIT | 8 674 | 2026-05-26 | Fázy dekompilácie (lifting, optimalizácie, štruktúrovanie). Označenie výstupu ako odhad. |
| pyelftools | https://github.com/eliben/pyelftools | Public domain / Unlicense (GitHub: NOASSERTION) | 2 290 | 2026-11-02 | Porovnanie s vlastným ELF parserom pri vývoji. |
| pefile | https://github.com/erocarrera/pefile | MIT | 2 090 | 2026-11-22 | Porovnanie s vlastným PE parserom pri vývoji (importy, exporty, relokácie). |
| Zydis | https://github.com/zyantific/zydis | MIT | 4 438 | 2026-11-13 | Alternatíva k Capstone pre x86. Neprevzaté (len x86). |
| iced | https://github.com/icedland/iced | MIT | 3 634 | 2026-11-22 | Alternatíva k Capstone pre x86. Neprevzaté (len x86). |
| Keystone | https://github.com/keystone-engine/keystone | GPL-2.0 | 2 663 | 2026-11-08 | Nepoužité. Assembler pre patch mód (roadmapa); GPL-2.0, preto len voliteľne. |
| Cytoscape.js | https://github.com/cytoscape/cytoscape.js | MIT | 11 300 | 2026-11-10 | Nepoužité. Zvažované pre grafy, nahradené vlastným SVG rozložením. |
| dagre | https://github.com/dagrejs/dagre | MIT | 5 863 | 2026-10-24 | Algoritmus vrstveného rozloženia (Sugiyama): vrstvy podľa najdlhšej cesty, zníženie krížení. Napísali sme vlastnú zjednodušenú verziu. |
| PySide6 | https://github.com/pyside/pyside-setup | LGPL-3.0 / GPL (podľa Qt) | 138 (zrkadlo) | 2026-11-20 | Zvažované pre GUI. Nepoužité (veľkosť, LGPL podmienky pri distribúcii). |

## Porovnanie kľúčových knižníc

| Rozhodnutie | Možnosti | Výber | Dôvod |
|---|---|---|---|
| Disassembler | Capstone, Zydis, iced, vlastný | **Capstone** | Viac architektúr (x86, ARM, AArch64, MIPS…), BSD-3, hotové Python wheels pre Linux, Windows aj macOS. Zydis a iced sú lepšie pre x86, ale nič iné nepodporujú. |
| Parsery ELF/PE | pyelftools, pefile, LIEF, vlastné | **vlastné** | Potrebovali sme odolnosť voči poškodeným súborom: každá chyba je diagnostika, nie výnimka. Žiadne ďalšie závislosti. pyelftools a pefile sme použili len na porovnanie počas vývoja. |
| GUI | PySide6/Qt, Tauri, Electron, lokálny web | **lokálny web (stdlib HTTP server + vanilla JS)** | Nulové ďalšie závislosti, rovnaké na všetkých OS, jednoduché testovanie (Playwright). Pozri [architecture.md](architecture.md). |
| Grafy | Cytoscape.js, dagre, Graphviz, vlastné | **vlastné SVG** | Malé grafy funkcií (desiatky blokov), bez CDN a bez npm build kroku. |
| Projekt | JSON, SQLite | **SQLite** | Atómové uloženie, verzia schémy, migrácie, je v štandardnej knižnici Pythonu. |

## Záznam použitia REA (`npx rea-agents`)

Špecifikácia odporúča použiť REA. Výsledok: **čiastočne funkčné**.

| Krok | Príkaz | Výsledok |
|---|---|---|
| Inštalácia | `npx -y rea-agents --help` | Funguje, verzia `rea@6.10.1`. |
| Diagnostika | `npx rea-agents doctor`, `capabilities` | Funguje. Natívne analýzy potrebujú externý provider (Ghidra, Hopper, IDA alebo kuna). |
| Analýza binárky | `npx rea-agents analyze /usr/bin/true` | **Nefunguje**: `code: provider_unavailable`, „Set GHIDRA_INSTALL_DIR to an extracted Ghidra 12.x (>=12.2) release directory." |
| Stiahnutie Ghidry | stiahnutie release z github.com | **Nefunguje**: proxy prostredia vrátila HTTP 403 pre release súbory. |
| Inšpekcia artefaktu | `npx rea-agents inspect-artifact libcapstone.so` a `… login_x64.elf` | Funguje: formát ELF, SHA-256 (`a0897f80…dc84` pre libcapstone.so), inventár bez spustenia súboru. |
| Audit vydania | `npx rea-agents audit-release-exposure libcapstone.so` | Funguje: 15 kontrol toho, čo prezradí statická analýza (napr. source mapy, debug súbory vedľa binárky). |

Keďže plná analýza cez REA nebola možná, pokračovali sme podľa pokynov cez
GitHub (metadáta a dokumentácia projektov vyššie) a nezávislú kontrolu cez radare2.

## Kontrola závislosti Capstone

`libcapstone.so` z Python balíka `capstone 5.0.9` sme pred použitím skontrolovali
staticky (`rabin2 -I`, `rabin2 -i`, radare2 5.5.0):

- x86-64, PIC, NX zapnuté, nie je stripped, partial RELRO, bez stack canary.
- Importy sú len z libc (malloc, free, memcpy, printf, strcmp…). **Žiadne sieťové
  funkcie** (socket, connect, getaddrinfo) a žiadne spúšťanie procesov.
- Licencia BSD-3-Clause je kompatibilná s MIT.

## Nezávislé overenie výsledkov analýzy (radare2)

Na ukážkových binárkach sme porovnali výsledky REVERSA s radare2 5.5.0
(`aaa; axt @ sym.imp.strcmp; afl`):

| Súbor | Volajúci `strcmp` (radare2) | Volajúci `strcmp` (REVERSA) | Zhoda |
|---|---|---|---|
| `login_x64.elf` | check_password, check_user | check_password, check_user | áno |
| `login_x64_stripped.elf` | 0x401196, 0x401217 | sub_401196, sub_401217 | áno |
| `login_arm64.elf` | check_password, check_user | check_password, check_user | áno |
| `login_x64.exe` (mingw PE) | radare2 s `axt @ sym.imp.strcmp` nenašiel nič (iné meno importu) | check_password, check_user | nedá sa porovnať |

Funkcie `main`, `sum_to`, `classify`, `greet` v `hello_x64.elf` našli oba nástroje
na rovnakých adresách. Očakávané hodnoty („golden files") sú v `samples/expected/`
a kontroluje ich `tests/test_analysis.py`.

## Čo sme odmietli

V súlade so špecifikáciou (sekcia 7) REVERSA neobsahuje a nebude obsahovať funkcie
na obchádzanie DRM, licenčných kontrol, anti-cheat systémov alebo ochrany proti
kopírovaniu. Rovnako neobsahuje telemetriu ani cloudové služby.
