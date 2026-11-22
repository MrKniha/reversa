# Stav projektu

Verzia **0.2.0** (prepis do C++ s Qt GUI), stav k 22. 11. 2026. Tento dokument
je záverečná kontrolná slučka podľa špecifikácie (sekcia 8.3). Každý bod má
dôkaz: test, príkaz a jeho výsledok. Stavy: **prešlo**, **neprešlo**,
**nebolo možné overiť**.

Overené lokálne (Linux x86-64, GCC 13, Qt 6.4.2, Python 3.13) a v GitHub
Actions na Ubuntu 24.04 (GCC, systémové Qt 6.4, Python 3.12) a Windows Server
2022 (MSVC 2022, Qt 6.8.3, Python 3.12).

## Súhrn kontrol

| Kontrola | Príkaz | Výsledok |
|---|---|---|
| C++ testy | `ctest` → `reversa_tests` | 42 testov, 0 zlyhalo (formáty, disassembler, analýza, fuzzing, dekompilátor + golden pseudokód, session, projekty) |
| GUI test | `ctest` → `reversa_gui_tests` (QTest, `QT_QPA_PLATFORM=offscreen`) | 6/6 prešlo: demo → analýza → navigácia → grafy → premenovanie funkcie aj premennej → komentár, záložka → hľadanie → undo/redo → uloženie a otvorenie projektu → chybový súbor → motív; zrušenie analýzy; pluginy na pracovnom vlákne; sprievodca a skratky |
| Python API testy | `pytest tests/python` (proti C++ jadru) | 80 prešlo, 0 zlyhalo |
| Licencie | `python3 scripts/check_licenses.py` | `Licenčná kontrola: PREŠLA` (Capstone, pybind11, SQLite, Qt, Python, testovacie nástroje) |
| Headless | `python3 scripts/headless_check.py build/bin/reversa` | `headless check OK: 10 samples` (reporty .md/.html, projekty, demo, skript) |
| Linux balík | `cpack -G DEB`, `apt install ./reversa_0.2.0_amd64.deb` | CI: inštalácia, `reversa --version`, skript z `/usr/share/reversa/examples`, `--demo --no-gui` |
| Windows balík | `scripts/package_windows.py` (windeployqt + Python runtime) | CI: zip, test z čistého `PATH` (len balík poskytuje Qt a Python): `--version`, skript, `--demo --no-gui` |
| CI na GitHube | `.github/workflows/ci.yml` (licencie, Linux, Windows) | prešlo, pozri [Záznam CI](#záznam-ci) |

## Akceptačné kritériá (8.2)

| # | Kritérium | Stav | Dôkaz |
|---|---|---|---|
| 1 | Inštalácia podľa README a `reversa --demo` do 5 minút | prešlo (automaticky), s reálnym používateľom **nebolo možné overiť** | Hotové balíky z CI (zip / .deb) sa inštalujú jedným krokom; CI ich po zostavení nainštaluje a spustí. |
| 2 | Do 3 minút funkcie, reťazce, x-refs, call graph, pseudokód `main` bez manuálu | prešlo (automaticky), s reálnym nováčikom **nebolo možné overiť** | `GuiTest::demo_workflow`: po analýze dema je vybraná `main`, zoznam funkcií, listing, pseudokód, x-refs v Detailoch, CFG aj graf volaní sa vykreslia. Sprievodca (5 krokov) pri prvom spustení. |
| 3 | Premenovanie sa prejaví všade a prežije uloženie | prešlo | C++ „rename propagates everywhere, undo and redo", „project roundtrip…"; Python `test_rename_propagates_everywhere_and_undo_redo`, `test_roundtrip_preserves_everything`; GUI: premenovanie → zoznam funkcií a listing, uloženie, opätovné otvorenie projektu. |
| 4 | ≤ 10-riadkový skript vypíše volajúcich `strcmp` | prešlo | `examples/scripts/strcmp_callers.py`: `test_strcmp_script_is_short`, `test_acceptance_script_strcmp_callers`; v CI cez `reversa script` aj z nainštalovaných balíkov. |
| 5 | Plugin z tutoriálu sa načíta, vykoná hook, jeho pád nezloží aplikáciu | prešlo | `test_tutorial_plugin_hooks`, `test_crashing_plugin_is_isolated`, `test_bad_plugins_never_stop_loading`, GUI `plugins_on_worker_thread`. |
| 6 | Poškodená binárka: nespadne, priateľská chyba, načíta, čo vie | prešlo | C++ „corrupted ELF header falls back to raw", „truncated PE reports but loads", fuzzing (mutácie, orezanie, náhodné dáta), „load errors"; GUI: prázdny súbor → priateľská chyba. |
| 7 | Tutoriál „prvá analýza za 10 minút" (walkthrough) | prešlo (dokument), s reálnym nováčikom **nebolo možné overiť** | README, `docs/quickstart.md`, `docs/tutorials.md`. Kroky zodpovedajú GUI testu. |
| 8 | `reversa analyze subor.exe --report report.md` headless v CI | prešlo | `test_analyze_report_markdown`, `scripts/headless_check.py` (aj `.exe` vzorky) v CI na Linuxe aj Windows. |
| 9 | Licenčná kontrola v CI, LICENSE-THIRD-PARTY kompletný | prešlo | Job `licenses` v CI. |
| 10 | Všetky testy v CI zelené, release build + balíčky pre 2 platformy | prešlo | [Záznam CI](#záznam-ci): Release build, `.deb` a Windows zip ako artefakty CI. |

## MUST-HAVE funkcie (4.1)

| # | Funkcia | Stav | Dôkaz |
|---|---|---|---|
| 1 | ELF, PE, surové dáta s ručnou architektúrou | prešlo | C++ „load all samples", „raw shellcode analysis"; `test_raw_analysis_with_manual_arch` |
| 2 | Detekcia architektúry, endianity, bitov, kompilátora | prešlo | C++ „headers of the hello samples", „format detection and raw arch guess", „PE details" |
| 3 | Disassembler s farebnou syntaxou a klikateľnými operandmi | prešlo | C++ „x86-64 flow and references", „arm64 adrp tracking and flow"; GUI listing s farbami a odkazmi (GUI test klikne cez odkazy, screenshoty `REVERSA_SHOTS`). |
| 4 | Funkcie: meno, adresa, veľkosť, počet volaní/x-refs | prešlo | Panel Funkcie (Meno, Adresa, Volaní, Veľkosť); C++ „CFG loops, call graph neighbourhood, function_containing" |
| 5 | Reťazce ASCII a UTF-16 s odkazmi | prešlo | `test_string_xrefs_point_into_functions`, golden súbory; karta Reťazce (stĺpec Odkazy) |
| 6 | Importy, exporty, relokácie, sekcie s hex dumpom | prešlo | C++ „ELF details", „PE details"; GUI karty Importy, Exporty, Sekcie a hex, *Pokročilé → Relokácie* |
| 7 | X-refs: volané z, čítané z, zapisované z | prešlo | C++ „string, data pointer and read xrefs", „x86 memory access kinds…"; panel Detaily |
| 8 | Graf volaní (filter, hľadanie, zoom) | prešlo | GUI karta Graf volaní (hĺbka, importy, zoom kolieskom, dvojklik); C++ call graph test |
| 9 | CFG so zvýraznením slučiek | prešlo | C++ „CFG loops…"; GUI: slučky oranžovo, spätné hrany prerušovane |
| 10 | Pseudokód: volania, vetvenia, slučky, premenné | prešlo | C++ dekompilátor testy + golden pseudokód všetkých 10 vzoriek; `test_every_function_decompiles_without_crash` |
| 11 | Premenovanie funkcií a premenných s propagáciou, undo/redo | prešlo | C++ a Python testy premenovania; GUI test (funkcia aj premenná, Späť/Znova) |
| 12 | Komentáre a záložky | prešlo | `test_comments_bookmarks_undo`; GUI test; karta Záložky |
| 13 | Hľadanie: reťazce, bajty, hex vzory, inštrukcie, regex v pseudokóde | prešlo | C++ „search: bytes, strings, instructions, pseudocode, errors"; `test_search`, `test_info_search_script`; GUI hľadanie a *Pokročilé* hľadania |
| 14 | Python API + 3 ukážkové skripty | prešlo | `test_example_scripts` (Python aj `reversa script`), `docs/scripting-tutorial.md` |
| 15 | Ukladanie projektu | prešlo | C++ „project roundtrip…", „project migrations…"; `test_reads_projects_written_by_reversa_0_1`, `test_tampered_binary_detected` |
| 16 | Report Markdown/HTML | prešlo | `test_analyze_report_markdown`, `test_analyze_html_json_project`; GUI *Súbor → Exportovať report* |
| 17 | Pluginy: priečinky, entry point, háčiky, verzia API | prešlo | `tests/python/test_plugins.py` (5 testov), GUI `plugins_on_worker_thread`, *Pokročilé → Pluginy* |
| 18 | Undo/redo v celom GUI | prešlo | GUI test: premenovanie, premenná, komentár, záložka vrátené cez príkaz Späť, potom Znova |
| 19 | Tmavý/svetlý motív, vlastné skratky | prešlo | GUI test: prepnutie motívu, uloženie vlastnej skratky (`Ctrl+R` pre Premenovať) a jej použitie v novom okne |
| 20 | Stabilný import poškodených súborov | prešlo | fuzz testy (3), „load errors", `test_corrupted_project` |

## Bezpečnostné hranice

| Hranica | Stav | Dôkaz |
|---|---|---|
| Analyzovaný súbor sa nespúšťa | prešlo | V kóde nie je spúšťanie procesov nad analyzovaným súborom (`src/` neobsahuje `system`, `popen`, `CreateProcess`). |
| Žiadna telemetria, žiadna sieť | prešlo | `src/` nepoužíva sieť; GUI je natívne, nepočúva na porte (webový server z 0.1 je odstránený). |
| Žiadne funkcie na obchádzanie DRM, licencií, anti-cheatu | prešlo | README, kapitola Etika. Takáto funkcia neexistuje. |
| Závislosti so zámkom a kontrolou licencií | prešlo | Pevné git tagy (Capstone, pybind11), vcpkg baseline, hashe v `tests/requirements-dev.lock`, Actions pripnuté na commity, job `licenses`. |
| Pluginy a skripty sú sandboxované | **neprešlo** (vedomé obmedzenie) | Izolácia chýb áno, sandbox nie. Zdokumentované v README, plugin-tutorial.md, SECURITY.md. Roadmapa. |

## Odchýlky od zadania

- Debugger nie je súčasťou (NICE-TO-HAVE, bod 22).
- GUI je len po slovensky. Texty sú v `tr()`, anglický preklad cez Qt Linguist
  je v pláne.
- Webové GUI z verzie 0.1 bolo nahradené natívnym Qt GUI (požiadavka „kód nech je
  v C++"). Python zostal pre skripty a pluginy.
- REA nemohla urobiť plnú analýzu (chýba Ghidra, sťahovanie blokované).
  Pozri [research.md](research.md).

## Známe obmedzenia

Pozri [architecture.md](architecture.md#známe-obmedzenia). Žiadne z nich nie je
kritické pre akceptačné kritériá.

## Záznam CI

Beh [45052061572](https://github.com/MrKniha/reversa/actions/runs/45052061572)
na vetve `cpp` (22. 11. 2026): **všetky joby zelené**.

| Job | Výsledok |
|---|---|
| licenses | prešlo |
| Linux (Ubuntu 24.04) | ctest 2/2 (42 C++ testov + GUI test), pytest 80/80, headless 10 vzoriek, `.deb` (2 MB) nainštalovaný a spustený |
| Windows (Server 2022, MSVC) | ctest 2/2, pytest 80/80, headless 10 vzoriek, zip (27 MB) otestovaný s čistým `PATH` |

Chyby nájdené v CI a opravené: chýbajúce `#include` pre MSVC, Python DLL sa
nedá linkovať s `/DELAYLOAD`, CRLF v golden súboroch (pridaný `.gitattributes`),
názov súboru v reporte pri Windows cestách, CLI test spúšťal GUI.

## Záznam postupu

| Dátum | Krok |
|---|---|
| 2026-11-22 | Verzia 0.1 v Pythone (pozri históriu gitu): výskum, jadro, dekompilátor, webové GUI, CI. |
| 2026-11-22 | Rozhodnutie používateľa: celý kód v C++. Prepis jadra (formáty, disassembler, analýza, dekompilátor) s overením zhody výstupov proti 0.1 na všetkých vzorkách. |
| 2026-11-22 | Session, SQLite projekty (kompatibilné s 0.1), reporty, CLI s rovnakými príkazmi a výstupmi. |
| 2026-11-22 | Python API cez pybind11, vstavaný interpret pre `reversa script` a pluginy; testy 0.1 prenesené do C++ a `tests/python`. |
| 2026-11-22 | Qt GUI, GUI test, balíčky (.deb, Windows zip), CI pre Linux a Windows, dokumentácia. |
