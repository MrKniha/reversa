# Architektúra REVERSA

Tento dokument popisuje, ako je REVERSA postavená, prečo sme sa tak rozhodli
a čo plánujeme ďalej. Pojmy vysvetľuje [slovník](glossary.md).

## Prehľad vrstiev

```
┌──────────────────────────────────────────────────────────────────────┐
│ Rozhrania:  Qt GUI (src/gui)   CLI (src/cli)   reporty (src/report)   │
│             pluginy a skripty v C/C++ (src/plugins, sdk/include)       │
├──────────────────────────────────────────────────────────────────────┤
│ Služby:     Session (src/session) – otvorenie, analýza, anotácie,      │
│             undo/redo, cache pseudokódu, uloženie projektu             │
│             EventBus a UndoStack (src/session/events)                  │
├──────────────────────────────────────────────────────────────────────┤
│ Analýza:    src/analysis – funkcie, bloky, x-refs, reťazce, grafy,     │
│             hľadanie      src/decompiler – IR, lifting, passy,         │
│             štruktúrovanie, renderer                                   │
├──────────────────────────────────────────────────────────────────────┤
│ Jadro:      src/formats – ELF, PE, surové dáta (vlastné parsery)       │
│             src/disasm – Capstone   src/core – Binary, JSON, text      │
│             src/project – SQLite projekt (.rvproj) s migráciami        │
└──────────────────────────────────────────────────────────────────────┘
```

Každá vrstva používa len vrstvy pod sebou. GUI, CLI aj pluginy volajú tú istú
`rv::Session`, preto je správanie všade rovnaké. Napríklad premenovanie zo
skriptu sa okamžite prejaví v GUI (cez EventBus).

## Moduly

| Modul | Zodpovednosť |
|---|---|
| `src/core` | `Binary`, `Section`, `Symbol`, `Import`, `Export`, `Relocation`, `Diagnostic`; `Reader` s kontrolou hraníc; chyby (`ReversaError` → `LoadError`, `ProjectError`, `AnnotationError`, `Cancelled`); vlastný `Json`; SHA-256; UTF-8 pomôcky. |
| `src/formats` | Parsery ELF, PE a surových dát, rozpoznanie formátu, odhad architektúry. Poškodená časť = diagnostika, zvyšok sa načíta. |
| `src/disasm` | Prevod Capstone inštrukcií na `Insn` (tok riadenia, cieľ, odkazy na pamäť). Sledovanie `adrp`+`add` pre ARM64. |
| `src/analysis` | Rekurzívny zostup od vstupných bodov, symbolov, exportov a `.pdata`. Noreturn funkcie, thunky PLT/IAT, hľadanie `main`, x-refs, reťazce ASCII a UTF-16, CFG, dominátory, slučky, graf volaní, hľadanie. |
| `src/decompiler` | `ir` (výrazy a príkazy), `lift` (x86, x86-64, AArch64), `passes` (propagácia, DCE), `structure` (if/else, while, do-while, goto), `render` (segmenty s typom a odkazom). |
| `src/project` | Anotácie a SQLite úložisko so schémou v1 a migráciami. Formát je kompatibilný s projektmi z verzie 0.1 (Python). |
| `src/session` | `Session`: mená, `resolve`, editácie cez `UndoStack`, udalosti, pohľady pluginov, uloženie. |
| `src/report` | Reporty Markdown, HTML a JSON súhrn. |
| `src/cli` | Príkazový riadok (`analyze`, `info`, `search`, `script`, `gui`, `--demo`). |
| `src/plugins` | Načítanie natívnych pluginov a skriptov (`dlopen` / `LoadLibraryW`), tabuľka funkcií `rv_host`, izolácia chybných pluginov. |
| `sdk/include/reversa` | Verejné SDK: `plugin.h` (stabilné C ABI, `RV_API_VERSION`) a `sdk.hpp` (C++ pomocník: `Program`, `Plugin`, makrá `REVERSA_PLUGIN`, `REVERSA_SCRIPT`). |
| `src/gui` | Qt Widgets: hlavné okno, pohľady (listing, pseudokód, detaily, hex), graf (`GraphView`), dialógy (sprievodca, nastavenia). |

## Dátové toky

**Otvorenie súboru:**
`súbor → detect_format → ELF/PE/RAW parser → Binary (+ diagnostiky)
→ pluginy on_load → analyze() na pracovnom vlákne (priebeh, zrušenie)
→ Analysis → pluginy on_function_analyzed → EventBus „analysis_done" → GUI / CLI`.

**Pseudokód:**
`funkcia → build_cfg → lifter (Insn → IR) → passy → štruktúrovanie
→ renderer (segmenty) → Session cache → GUI (klikateľné mená a premenné) / text`.
Cache sa zneplatní pri premenovaní, takže nové meno sa zobrazí všade.

**Premenovanie:**
`GUI/API → Session::rename → validácia → Command na UndoStack
→ Annotations → EventBus „renamed" → pluginy on_rename → GUI obnoví pohľady`.

**Uloženie projektu:**
`Session::save → save_project → dočasný súbor → atómové premenovanie`.
Projekt obsahuje SHA-256 binárky. Pri otvorení sa overí, či binárka sedí.

## Prečo C++ a Qt

Verzia 0.1 bola v Pythone s webovým GUI v prehliadači. Od verzie 0.2 je celý
nástroj v C++20 s natívnym rozhraním Qt 6, vrátane skriptov a pluginov.

- **Výkon a pamäť.** Analýza, x-refs a dekompilátor bežia natívne, bez
  interpretera. Veľké binárky (desaťtisíce funkcií) sú realistické, model
  dát je kompaktný (`std::map`, `unordered_map`, žiadne objekty na inštrukciu
  navyše).
- **Jeden program bez závislostí.** Windows balík je zip s `reversa-gui.exe`;
  Linux balík je `.deb` so systémovými knižnicami. Netreba inštalovať Python
  ani prehliadač.
- **Natívne GUI.** Qt Widgets je overené v nástrojoch ako Cutter, má
  dokovateľné panely, klávesové skratky, tmavý motív a funguje rovnako na
  Windows aj Linuxe. Odpadá lokálny HTTP server a s ním celá jeho útočná plocha.
- **Pluginy a skripty v C/C++ cez stabilné C ABI.** Plugin aj skript je zdieľaná
  knižnica, ktorá s REVERSA komunikuje len cez tabuľku funkcií `rv_host`
  (`sdk/include/reversa/plugin.h`). Nepotrebuje linkovať REVERSA, funguje
  s ľubovoľným kompilátorom C alebo C++ a staré pluginy fungujú aj v novších
  verziách: funkcie sa pridávajú len na koniec tabuľky a nekompatibilná zmena
  zvýši `RV_API_VERSION`. Pluginy a skripty v Pythone z verzie 0.1 už nie sú
  podporované (rozhodnutie 2026-11-22: celý nástroj v C++).
- **Licencie.** Qt je pod LGPL-3.0 a linkujeme ho dynamicky (vymeniteľné
  knižnice), čo je s MIT licenciou REVERSA v poriadku. Capstone je
  BSD-3-Clause, SQLite public domain.

Cena: zostavenie je zložitejšie (CMake, kompilátor, Qt). Preto CI zostavuje
hotové balíky pre Windows aj Linux a README popisuje zostavenie krok za krokom.

## Ďalšie rozhodnutia

**Capstone.** Jeden disassembler pre x86, x86-64, ARM, AArch64 a ďalšie
architektúry, licencia BSD-3. Zydis a iced sú pre x86 presnejšie, ale ostatné
architektúry by vyžadovali ďalšiu knižnicu. Linkujeme ho staticky, len so
štyrmi architektúrami, ktoré používame.

**Vlastné parsery ELF a PE.** Pri analýze malvéru sú poškodené a zámerne
pokazené hlavičky bežné. Vlastný parser s kontrolou hraníc pri každom čítaní
vie načítať to, čo sa dá, a zvyšok ohlási ako diagnostiku. Fuzz testy
(`tests/cpp/test_analysis.cpp`) to overujú.

**Vlastné rozloženie grafov.** Grafy funkcií majú zvyčajne desiatky blokov.
Zjednodušený vrstvený algoritmus (podľa dagre a Sugiyamu) v `QGraphicsView`
na to stačí. Pre veľké grafy (stovky blokov) bude treba lepší algoritmus
(roadmapa).

**SQLite pre projekty.** Jeden súbor, atómové transakcie, verzia schémy
a migrácie. JSON by bol jednoduchší, ale pri veľkých projektoch pomalší
a bez transakcií.

**Medzijazyk (IR) v dekompilátore.** Lifter pre každú architektúru prevedie
inštrukcie do spoločného IR. Optimalizácie a štruktúrovanie sú potom spoločné.
Novú architektúru pridáte napísaním jedného liftera.

**Závislosti so zámkom.** Capstone sťahuje CMake v pevnom git tagu, SQLite na
Windows ide cez vcpkg s pevným `builtin-baseline` a GitHub Actions sú pripnuté
na commity. Testy a nástroje repozitára (`tools/*.cpp`) nepotrebujú nič navyše.
`tools/check_licenses.cpp` v CI overí licenciu každej závislosti.

**MIT licencia.** Najjednoduchšia pre používateľov aj firmy, kompatibilná so
všetkými našimi závislosťami. GPL nástroje (Unicorn, Keystone) preto nemôžu byť
povinnou súčasťou, len voliteľnými pluginmi.

**Izolácia pluginov bez sandboxu.** Háčik, ktorý vráti chybu (makro
`REVERSA_PLUGIN` na ňu prevedie aj výnimku), plugin vypne a REVERSA beží ďalej.
Knižnicu, ktorá sa nedá načítať alebo má inú verziu API, REVERSA preskočí
a ohlási. Pád pamäte v natívnom plugine však ukončí celú aplikáciu. Háčiky
počas analýzy bežia na pracovnom vlákne, volania na jeden program sú
serializované. Skutočný sandbox (samostatný proces) je v roadmape. Dovtedy
dokumentácia jasne varuje, že pluginy sú dôveryhodný kód.

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
- Žiadna sieť, žiadna telemetria. GUI je natívne, nič nepočúva na porte.
- Všetky čítania zo súboru idú cez kontrolu hraníc; fuzz testy v CI.
- Pluginy a skripty nie sú sandboxované (pozri vyššie). Načítajú sa len
  z používateľského priečinka pluginov a z priečinkov zadaných voľbou
  `--plugins`; voľba `--no-plugins` ich vypne.

## Plán priorít (MVP, sekcia 4.2)

| Priorita | Funkcia | Stav v 0.2.0 |
|---|---|---|
| P0 | Načítanie ELF/PE, odolnosť voči poškodeniu | hotové |
| P0 | Disassembly x86, x86-64, ARM64 (ARM 32-bit základ) | hotové |
| P0 | Funkcie, reťazce, importy, exporty, x-refs | hotové |
| P0 | CFG a graf volaní | hotové |
| P0 | Pseudokód (dočasný) | hotové |
| P0 | Premenovanie, komentáre, záložky, undo/redo, projekt | hotové |
| P0 | API pre skripty, pluginy s izoláciou (C/C++) | hotové |
| P0 | Headless CLI, reporty | hotové |
| P1 | Slovenské a anglické rozhranie | slovenské úplné; anglický preklad chýba (texty sú pripravené cez `tr()`) |
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

## Známe obmedzenia

- `main` sa nenájde v stripped PE z mingw (chýba vzor ako `__libc_start_main`).
- Jump tabuľky sa neriešia, nepriame skoky končia blok.
- Floating point a SIMD inštrukcie sú v pseudokóde ako `__asm(...)`.
- Pluginy nie sú sandboxované.
- Mach-O, APK a iné formáty sa načítajú ako surové dáta.
- GUI je zatiaľ len po slovensky (anglický preklad cez Qt Linguist je v pláne).
- Listing v GUI zobrazuje jednu funkciu naraz; dáta mimo funkcií ukazuje hex
  výpis na karte Sekcie a hex.
