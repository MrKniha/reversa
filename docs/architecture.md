# Architektúra REVERSA

Tento dokument popisuje, ako je REVERSA postavená, prečo sme sa tak rozhodli
a čo plánujeme ďalej. Pojmy vysvetľuje [slovník](glossary.md).

## Prehľad vrstiev

```
┌──────────────────────────────────────────────────────────────────────┐
│ Rozhrania:  webové GUI (ui/)   CLI (cli.py)   Python API (api/)       │
│             pluginy (plugins/)  reporty (report.py)                    │
├──────────────────────────────────────────────────────────────────────┤
│ Služby:     Session (session.py) – otvorenie, analýza, anotácie,       │
│             undo/redo, cache pseudokódu, uloženie projektu             │
│             EventBus (core/events.py), UndoStack (core/undo.py)        │
├──────────────────────────────────────────────────────────────────────┤
│ Analýza:    analysis/ – funkcie, bloky, x-refs, reťazce, grafy,        │
│             hľadanie        decompiler/ – IR, lifting, passy,          │
│             štruktúrovanie, renderer                                   │
├──────────────────────────────────────────────────────────────────────┤
│ Jadro:      formats/ – ELF, PE, surové dáta (vlastné parsery)          │
│             disasm/ – Capstone backend    core/model.py – Binary       │
│             project/ – SQLite projekt (.rvproj) s migráciami           │
└──────────────────────────────────────────────────────────────────────┘
```

Každá vrstva používa len vrstvy pod sebou. GUI, CLI aj pluginy volajú tú istú
`Session`, preto je správanie všade rovnaké. Napríklad premenovanie zo skriptu
sa prejaví v GUI.

## Moduly

| Modul | Zodpovednosť |
|---|---|
| `core/model.py` | `Binary`, `Section`, `Symbol`, `Import`, `Export`, `Relocation`, `Diagnostic`. Čítanie pamäte podľa adresy (bisect cez sekcie). |
| `core/errors.py` | Hierarchia chýb (`ReversaError` → `LoadError`, `ProjectError`, `AnnotationError`, `Cancelled`) so zrozumiteľnou správou a technickým detailom. |
| `core/events.py` | `EventBus`: udalosti `renamed`, `commented`, `analysis_done`… Chyba jedného poslucháča neovplyvní ostatných. |
| `core/undo.py` | `UndoStack` s príkazmi (command pattern). |
| `formats/reader.py` | `Reader` s kontrolou hraníc. Každé čítanie mimo súboru je `ParseError`. |
| `formats/elf.py`, `pe.py`, `raw.py` | Parsery. Poškodená časť = diagnostika, zvyšok sa načíta. |
| `formats/loader.py` | Rozpoznanie formátu, fallback na surové dáta. |
| `disasm/capstone_backend.py` | Prevod Capstone inštrukcií na `Insn` (tok riadenia, cieľ, odkazy). Sledovanie `adrp`+`add` pre ARM64. |
| `analysis/analyzer.py` | Rekurzívny zostup od vstupných bodov, symbolov, exportov a `.pdata`. Noreturn funkcie, thunky PLT/IAT, hľadanie `main`. |
| `analysis/xrefs.py`, `strings.py`, `graphs.py`, `search.py` | X-refs (call/jump/read/write/addr/data), reťazce ASCII a UTF-16, CFG a graf volaní, dominátory a slučky, hľadanie. |
| `decompiler/` | `ir.py` (výrazy a príkazy), `lift_x86.py`, `lift_arm64.py`, `passes.py` (liveness, propagácia, DCE), `structure.py` (if/else, while, do-while, goto), `render.py` (segmenty s typom a odkazom). |
| `project/` | Anotácie (mená, komentáre, záložky, mená premenných) a SQLite úložisko so schémou v1 a migráciami. |
| `plugins/manager.py` | Načítanie `plugin.json`, kontrola verzie API, izolované volanie háčikov. |
| `api/program.py` | Verejné API `Program`, `FunctionView`, `XrefView`. |
| `ui/server.py`, `ui/static/` | Lokálny HTTP server a jednostránková aplikácia (HTML, CSS, JS bez frameworku). |
| `cli.py`, `report.py` | Príkazový riadok a reporty (Markdown, HTML, JSON). |

## Dátové toky

**Otvorenie súboru:**
`súbor → loader.detect_format → ELF/PE/RAW parser → Binary (+ diagnostiky)
→ pluginy on_load → analyze() → Analysis → pluginy on_function_analyzed
→ EventBus „analysis_done" → GUI / CLI`.

**Pseudokód:**
`funkcia → build_cfg → lifter (Insn → IR) → passy → štruktúrovanie
→ renderer (segmenty) → Session cache → GUI (klikateľné mená) / text`.
Cache sa zneplatní pri premenovaní, takže nové meno sa zobrazí všade.

**Premenovanie:**
`GUI/API → Session.rename → validácia → Command na UndoStack
→ Annotations → EventBus „renamed" → pluginy on_rename → GUI obnoví pohľady`.

**Uloženie projektu:**
`Session.save → project/store.save_project → dočasný súbor → atómové
premenovanie`. Projekt obsahuje SHA-256 binárky. Pri otvorení sa overí, či
binárka sedí.

## Prečo takto

**Python namiesto Rust/C++.** Cieľová skupina sú aj začiatočníci a ľudia,
ktorí chcú písať skripty. Python je jazyk skriptov aj jadra, takže API
nie je len tenká vrstva nad cudzím jadrom. Výkon stačí pre bežné binárky
(analýza ukážok trvá milisekundy). Najnáročnejšia časť, disassembler, je
v C (Capstone). Ak bude výkon problém, môžeme horúce časti prepísať do Rustu
bez zmeny API.

**Capstone.** Jeden disassembler pre x86, x86-64, ARM, AArch64 a ďalšie
architektúry, licencia BSD-3, hotové wheels pre Linux, Windows a macOS. Zydis
a iced sú pre x86 presnejšie, ale ostatné architektúry by vyžadovali ďalšiu
knižnicu.

**Vlastné parsery ELF a PE.** Pri analýze malvéru sú poškodené a zámerne
pokazené hlavičky bežné. Vlastný parser s kontrolou hraníc pri každom čítaní
vie načítať to, čo sa dá, a zvyšok ohlási ako diagnostiku. Fuzz testy
(`tests/test_fuzz.py`) to overujú. Navyše nepridávame závislosti.

**Lokálne webové GUI namiesto PySide6 alebo Tauri.** Server zo štandardnej
knižnice beží len na `127.0.0.1`, s náhodným tokenom a kontrolou hlavičky
`Host`. Rozhranie je čisté HTML, CSS a JavaScript. Výhody: žiadna ďalšia
závislosť (PySide6 má stovky MB a podmienky LGPL), rovnaký vzhľad na všetkých
OS, automatické UI testy cez Playwright. Nevýhoda: potrebuje prehliadač.

**Vlastné SVG rozloženie grafov.** Grafy funkcií majú zvyčajne desiatky blokov.
Zjednodušený vrstvený algoritmus (podľa dagre a Sugiyamu) na to stačí a
nepotrebuje CDN ani npm build. Pre veľké grafy (stovky blokov) bude treba lepší
algoritmus (roadmapa).

**SQLite pre projekty.** Jeden súbor, atómové transakcie, verzia schémy
a migrácie. SQLite je v štandardnej knižnici Pythonu. JSON by bol jednoduchší,
ale pri veľkých projektoch pomalší a bez transakcií.

**Medzijazyk (IR) v dekompilátore.** Lifter pre každú architektúru prevedie
inštrukcie do spoločného IR. Optimalizácie a štruktúrovanie sú potom spoločné.
Novú architektúru pridáte napísaním jedného liftera.

**MIT licencia.** Najjednoduchšia pre používateľov aj firmy, kompatibilná so
všetkými našimi závislosťami (Capstone BSD-3). GPL nástroje (Unicorn, Keystone)
preto nemôžu byť povinnou súčasťou, len voliteľnými pluginmi.

**Izolácia pluginov bez sandboxu.** Každý háčik je obalený `try/except`.
Spadnutý plugin sa vypne a REVERSA beží ďalej. Skutočný sandbox (samostatný
proces) je v roadmape. Do vtedy dokumentácia jasne varuje, že pluginy sú
dôveryhodný kód.

## Čo sme sa naučili z existujúcich projektov

- **Ghidra**: oddeliť IR od architektúry (P-code). Ukazovať pseudokód a listing
  vedľa seba a synchronizovať ich.
- **Cutter a IDA**: jedna „aktuálna adresa" riadi všetky pohľady. Rozloženie
  funkcie vľavo, obsah v strede, detaily vpravo.
- **radare2 a Rizin**: krátke klávesové skratky. Projekt ako samostatný súbor
  s verziou formátu.
- **angr**: rekurzívny zostup a zoznam noreturn funkcií sú dôležitejšie než
  heuristiky prológov. Python API ako plnohodnotné rozhranie.
- **Detect It Easy**: detekcia kompilátora a packera je pre začiatočníka
  cenná informácia hneď po otvorení súboru.
- **RetDec**: výstup dekompilátora treba jasne označiť ako odhad.
- **binwalk**: firmvér je často len surové dáta, treba ručne zadať architektúru
  a bázovú adresu.
- **LIEF, pyelftools, pefile**: jednotný model binárky pre všetky formáty.
  Zároveň sme videli, že knižnice, ktoré pri chybe vyhodia výnimku, nie sú
  vhodné pre poškodené súbory.

## Bezpečnostný model

- Iba statická analýza. Analyzovaný súbor sa nikdy nespúšťa.
- Žiadna sieť okrem lokálneho servera GUI na `127.0.0.1`. Žiadna telemetria.
- Server GUI: náhodný token pre každú požiadavku na `/api/`, kontrola `Host`
  (ochrana pred DNS rebinding), bez prístupu mimo statických súborov
  a `docs/`.
- Upload v GUI ukladá súbory do dočasného priečinka, nespúšťa ich.
- Pluginy a skripty nie sú sandboxované (pozri vyššie).

## Plán priorít (MVP, sekcia 4.2)

| Priorita | Funkcia | Stav v 0.1.0 |
|---|---|---|
| P0 | Načítanie ELF/PE, odolnosť voči poškodeniu | hotové |
| P0 | Disassembly x86, x86-64, ARM64 (ARM 32-bit základ) | hotové |
| P0 | Funkcie, reťazce, importy, exporty, x-refs | hotové |
| P0 | CFG a graf volaní | hotové |
| P0 | Pseudokód (dočasný) | hotové |
| P0 | Premenovanie, komentáre, záložky, undo/redo, projekt | hotové |
| P0 | Python API, pluginy s izoláciou | hotové |
| P0 | Headless CLI, reporty | hotové |
| P1 | Slovenské a anglické rozhranie | slovenské úplné, anglické čiastočné |
| P1 | Onboarding, nápoveda, slovník | hotové |

## Roadmapa (sekcia 4.3, mimo MVP)

1. Mach-O a APK/DEX (teraz sa načítajú len ako surové dáta).
2. Jump tabuľky (`switch`), typy a štruktúry, signatúry knižníc (FLIRT-like).
3. Lepší dekompilátor: SSA, typová inferencia, floating point (teraz `__asm`).
4. Sandbox pre pluginy (samostatný proces).
5. Diffing dvoch verzií binárky.
6. Emulácia (voliteľný plugin s Unicorn, GPL-2.0).
7. **Patch mód**: úpravy vlastných binárok na opravu chýb a výučbu. Nikdy
   nebude obsahovať funkcie na obchádzanie DRM, licencií, anti-cheatu
   alebo ochrany proti kopírovaniu.
8. Debugger (lokálny, v sandboxe), kolaborácia na projekte.

## Známe obmedzenia 0.1.0

- `main` sa nenájde v stripped PE z mingw (chýba vzor ako `__libc_start_main`).
- Jump tabuľky sa neriešia, nepriame skoky končia blok.
- Floating point a SIMD inštrukcie sú v pseudokóde ako `__asm(...)`.
- Pluginy nie sú sandboxované.
- Mach-O, APK a iné formáty sa načítajú ako surové dáta.
- Anglický preklad GUI je čiastočný (chýbajúce texty sú po slovensky).
