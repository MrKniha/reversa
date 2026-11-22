# Stav projektu

Verzia **0.1.0 (MVP)**, stav k 22. 11. 2026. Tento dokument je záverečná
kontrolná slučka podľa špecifikácie (sekcia 8.3). Každý bod má dôkaz: test,
príkaz a jeho výsledok. Stavy: **prešlo**, **neprešlo**, **nebolo možné overiť**.

Všetko bolo overené v jednom prostredí: Linux x86-64, Python 3.13,
Capstone 5.0.9, Chromium (Playwright 1.63) pre UI test.

## Súhrn kontrol

| Kontrola | Príkaz | Výsledok |
|---|---|---|
| Testy | `coverage run -m pytest` | 126 testov prešlo, 0 zlyhalo |
| Pokrytie kódu | `coverage report` | 93 % celkovo (cieľ ≥ 80 %) |
| UI smoke test | `node tests/ui/smoke.mjs` | `UI smoke test: OK` |
| Licencie | `python scripts/check_licenses.py` | `Licenčná kontrola: PREŠLA` |
| Headless | `sh scripts/headless_check.sh` | `headless check OK` (11 vzoriek, reporty .md/.html, projekty) |
| Wheel | `pip wheel . --no-deps` | `reversa-0.1.0-py3-none-any.whl` |
| Čistá inštalácia | nový venv, `pip install --require-hashes -r requirements.lock`, wheel, `reversa --demo --no-gui` | prešlo |
| CI na GitHube | `.github/workflows/ci.yml` (Linux, Windows, macOS) | **nebolo možné overiť**: repozitár ešte nie je na GitHube |

## Akceptačné kritériá (8.2)

| # | Kritérium | Stav | Dôkaz |
|---|---|---|---|
| 1 | Inštalácia podľa README a `reversa --demo` do 5 minút | prešlo (Linux) | Čistý venv + wheel + `reversa --demo --no-gui`. GUI demo: `tests/ui/smoke.mjs`. Inštrukcie pre Windows a macOS sú v README, ale na týchto OS **nebolo možné overiť**. |
| 2 | Do 3 minút funkcie, reťazce, x-refs, call graph, pseudokód `main` bez manuálu | prešlo (automaticky), s reálnym nováčikom **nebolo možné overiť** | `smoke.mjs`: po otvorení dema je vybraná `main`, zoznam funkcií, pseudokód, x-refs v Detailoch, call graph aj CFG sa vykreslia. Onboarding (5 krokov) je v GUI. |
| 3 | Premenovanie sa prejaví všade a prežije uloženie | prešlo | `test_rename_propagates_everywhere_and_undo_redo`, `test_roundtrip_preserves_everything`, `smoke.mjs` (premenovanie v GUI → zoznam funkcií aj pseudokód `main`, uloženie, undo). |
| 4 | ≤ 10-riadkový skript vypíše volajúcich `strcmp` | prešlo | `examples/scripts/strcmp_callers.py` (5 riadkov kódu, 11 s docstringom): `test_strcmp_script_is_short`, `test_acceptance_script_strcmp_callers`. Výsledok zhodný s radare2 (pozri research.md). |
| 5 | Plugin z tutoriálu sa načíta, vykoná hook, jeho pád nezloží aplikáciu | prešlo | `test_tutorial_plugin_hooks`, `test_crashing_plugin_is_isolated`, `test_bad_plugins_never_stop_loading`. |
| 6 | Poškodená binárka: nespadne, priateľská chyba, načíta, čo vie | prešlo | `test_corrupted_elf_header_falls_back_to_raw`, `test_truncated_pe_reports_but_loads`, fuzzing `test_mutations_never_crash`, `test_truncation_at_every_boundary`, `test_random_garbage`, GUI `test_cancel_and_broken_file`. |
| 7 | Tutoriál „prvá analýza za 10 minút" (walkthrough) | prešlo (dokument), s reálnym nováčikom **nebolo možné overiť** | README, `docs/quickstart.md`, `docs/tutorials.md` (Tutoriál 1). Kroky zodpovedajú UI krokom v `smoke.mjs`. |
| 8 | `reversa analyze subor.exe --report report.md` headless v CI | prešlo lokálne, v CI **nebolo možné overiť** | `test_analyze_report_markdown`, `scripts/headless_check.sh` (aj `.exe` vzorky). |
| 9 | Licenčná kontrola v CI, LICENSE-THIRD-PARTY kompletný | prešlo lokálne, v CI **nebolo možné overiť** | `scripts/check_licenses.py` → PREŠLA. Jediná runtime závislosť: capstone (BSD-3-Clause). |
| 10 | Všetky testy v CI zelené, release build + balíčky pre 2 platformy | **nebolo možné overiť** | Lokálne 126/126 testov. Wheel je čistý Python (`py3-none-any`), teda inštalovateľný na Linux, Windows aj macOS, ale otestovaný len na Linuxe. CI matica pre 3 OS je pripravená. |

## MUST-HAVE funkcie (4.1)

| # | Funkcia | Stav | Dôkaz |
|---|---|---|---|
| 1 | ELF, PE, surové dáta s ručnou architektúrou | prešlo | `test_all_samples_load`, `test_raw_analysis_with_manual_arch`, `test_raw_shellcode_analysis` |
| 2 | Detekcia architektúry, endianity, bitov, kompilátora | prešlo | `test_headers`, `test_detect_and_raw` (prológy), `test_pe_details` (kompilátor) |
| 3 | Disassembler s farebnou syntaxou a klikateľnými operandmi | prešlo | `test_x86_64_flow_and_refs`, `test_arm64_adrp_tracking_and_flow`. GUI: zvýraznenie a odkazy v listingu (`smoke.mjs`, screenshoty). |
| 4 | Funkcie: meno, adresa, veľkosť, počet volaní/x-refs | prešlo | `test_function_containing_and_sizes`, `test_state_and_tables` |
| 5 | Reťazce ASCII a UTF-16 s odkazmi | prešlo | `test_string_xrefs_point_into_functions`, golden files |
| 6 | Importy, exporty, relokácie, sekcie s hex dumpom | prešlo | `test_elf_details`, `test_pe_details`, `test_state_and_tables` |
| 7 | X-refs: volané z, čítané z, zapisované z | prešlo | `test_read_write_xrefs_on_global`, `test_data_pointer_xrefs` |
| 8 | Graf volaní (filter, hľadanie, zoom) | prešlo | `test_callgraph_neighbourhood`, `test_listing_cfg_callgraph_pseudocode`, `smoke.mjs` |
| 9 | CFG so zvýraznením slučiek | prešlo | `test_cfg_loop_detection` |
| 10 | Pseudokód: volania, vetvenia, slučky, premenné | prešlo | `test_sum_to_is_a_while_loop`, `test_classify_if_else`, `test_main_calls_with_arguments`, `test_every_function_decompiles_without_crash` |
| 11 | Premenovanie funkcií a premenných s propagáciou, undo/redo | prešlo | `test_rename_propagates_everywhere_and_undo_redo`, `test_variable_rename_propagates_and_undoes` |
| 12 | Komentáre a záložky | prešlo | `test_comments_bookmarks_undo` |
| 13 | Hľadanie: reťazce, bajty, hex vzory, inštrukcie, regex v pseudokóde | prešlo | `test_search`, `test_search_and_report`, `test_info_search_script` |
| 14 | Python API + 3 ukážkové skripty | prešlo | `test_example_scripts`, `docs/scripting-tutorial.md` |
| 15 | Ukladanie projektu | prešlo | `test_roundtrip_preserves_everything`, `test_migrations_applied`, `test_tampered_binary_detected` |
| 16 | Report Markdown/HTML | prešlo | `test_analyze_report_markdown`, `test_analyze_html_json_project` |
| 17 | Pluginy: priečinky, entry point, háčiky, verzia API | prešlo | `tests/test_plugins.py` (5 testov) |
| 18 | Undo/redo v celom GUI | prešlo | `test_mutations` (server), `smoke.mjs` (tlačidlo Späť) |
| 19 | Tmavý/svetlý motív, vlastné skratky | prešlo | `smoke.mjs` (prepnutie motívu). Skratky: Nastavenia v GUI, uložené v prehliadači. Zmena skratky nie je pokrytá automatickým testom. |
| 20 | Stabilný import poškodených súborov | prešlo | fuzz testy (3), `test_load_errors`, `test_corrupted_project` |

## Bezpečnostné hranice

| Hranica | Stav | Dôkaz |
|---|---|---|
| Analyzovaný súbor sa nespúšťa | prešlo | V kóde nie je `subprocess`/`exec` nad analyzovaným súborom. Upload ukladá do `tempfile.mkdtemp`. |
| GUI len na 127.0.0.1, token, kontrola `Host` | prešlo | `test_http_server_security_and_flow` (403 bez tokenu, 403 cudzí Host, 404 path traversal) |
| Žiadna telemetria, žiadna sieť | prešlo | Žiadne sieťové volania v `src/` okrem lokálneho servera. Capstone bez sieťových importov (research.md). |
| Žiadne funkcie na obchádzanie DRM, licencií, anti-cheatu | prešlo | README, kapitola Etika. Takáto funkcia neexistuje. |
| Pluginy a skripty sú sandboxované | **neprešlo** (vedomé obmedzenie MVP) | Izolácia chýb áno, sandbox nie. Jasne zdokumentované v README, plugin-tutorial.md, SECURITY.md. Roadmapa. |

## Odchýlky od zadania

- Priečinok `src/reversa/debugger/` nie je vytvorený. Debugger je NICE-TO-HAVE
  (bod 22), nie je súčasťou MVP. Prázdny modul by bol len „TODO".
- Štruktúra `formats/elf/`, `formats/pe/` je zjednodušená na súbory
  `formats/elf.py`, `formats/pe.py`.
- REA nemohla urobiť plnú analýzu (chýba Ghidra, sťahovanie blokované).
  Pozri [research.md](research.md).
- Anglický preklad GUI je čiastočný.

## Známe obmedzenia

Pozri [architecture.md](architecture.md#známe-obmedzenia-010). Žiadna
z nich nie je kritická pre akceptačné kritériá.

## Záznam postupu

| Dátum | Krok |
|---|---|
| 2026-11-22 | Výskum: GitHub API (20 projektov), REA 6.10.1 (čiastočne), radare2 5.5.0 |
| 2026-11-22 | Jadro: model, ELF/PE/RAW parsery, Capstone backend, analýza, x-refs, grafy |
| 2026-11-22 | Dekompilátor: IR, liftery x86/x86-64/AArch64, passy, štruktúrovanie |
| 2026-11-22 | Session, undo/redo, SQLite projekt s migráciami, Python API, pluginy |
| 2026-11-22 | CLI, reporty, webové GUI, onboarding, slovenské texty |
| 2026-11-22 | Fuzzing odhalil pomalú analýzu poškodených súborov. Opravené (index sekcií, cache dekódovania). |
| 2026-11-22 | Oprava: predvolené mená `sub_…` sa dali zobraziť, ale nie vyhľadať. Regresný test pridaný. |
| 2026-11-22 | Dokumentácia, CI, lock súbor s hashmi, headless skript, wheel, záverečná kontrola |
