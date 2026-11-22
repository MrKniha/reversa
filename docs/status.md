# Project status

Version **0.2.0** (C++ rewrite with a Qt GUI), status as of 22 Nov 2026. This document is the
final check loop required by the specification (section 8.3). Every item has evidence: a test, a
command and its result. States: **passed**, **failed**, **could not be verified**.

Verified locally (Linux x86-64, GCC 13, Qt 6.4.2) and in GitHub Actions on Ubuntu 24.04 (GCC,
system Qt 6.4) and Windows Server 2022 (MSVC 2022, Qt 6.8.3).

## Summary of checks

| Check | Command | Result |
|---|---|---|
| C++ tests | `ctest` → `reversa_tests` | 59 tests, 0 failed (formats, disassembler, analysis, fuzzing, decompiler + golden pseudocode, golden facts of the samples, session, projects, C ABI and C++ SDK, plugins including broken ones, scripts, CLI) |
| GUI test | `ctest` → `reversa_gui_tests` (QTest, `QT_QPA_PLATFORM=offscreen`) | 7/7 passed: demo → analysis → navigation → graphs → rename of a function and a variable → comment, bookmark → search → undo/redo → save and reopen the project → broken file → theme; cancelling the analysis; plugins on the worker thread; guide and shortcuts; languages |
| Licenses | `check_licenses .` (tools/check_licenses.cpp) | `License check: PASSED` (Capstone, SQLite, Qt) |
| GUI translations | `i18n_check .` (tools/i18n_check.cpp) | `219 texts, 4 languages: OK` (sk, de, fr, zh; English is the source language) |
| Headless | `headless_check build/bin/reversa .` (tools/headless_check.cpp) | `headless check OK: 10 samples` (.md/.html reports, projects, demo, C++ script) |
| Linux package | `cpack -G DEB`, `apt install ./reversa_0.2.0_amd64.deb` | CI: install, `reversa --version`, script from `/usr/lib/reversa/examples`, a plugin built only against the installed SDK headers, `--demo --no-gui` |
| Windows package | `cpack -G ZIP` (windeployqt) | CI: zip, test with a clean `PATH` (only the package provides Qt): `--version`, `.dll` script, `--demo --no-gui` |
| CI on GitHub | `.github/workflows/ci.yml` (licenses, Linux, Windows) | passed, see [CI log](#ci-log) |

## Acceptance criteria (8.2)

| # | Criterion | State | Evidence |
|---|---|---|---|
| 1 | Install following the README and `reversa --demo` within 5 minutes | passed (automatically); with a real user **could not be verified** | The ready-made packages from CI (zip / .deb) install in one step; CI installs and runs them after the build. |
| 2 | Within 3 minutes: functions, strings, x-refs, call graph, pseudocode of `main` without a manual | passed (automatically); with a real beginner **could not be verified** | `GuiTest::demo_workflow`: after analysing the demo, `main` is selected and the function list, listing, pseudocode, x-refs in Details, CFG and call graph are drawn. Guide (5 steps) on the first start. |
| 3 | A rename shows up everywhere and survives saving | passed | C++ "rename propagates everywhere, undo and redo", "project roundtrip…", "C ABI host…" (rename from a plugin, undo); GUI: rename → function list and listing, save, reopen the project. |
| 4 | A script of ≤ 10 lines prints the callers of `strcmp` | passed | `examples/scripts/strcmp_callers.cpp` (8 lines of C++): "scripts: run…", "cli: C++ scripts and plugins…"; in CI through `reversa script`, also from the installed packages. |
| 5 | The tutorial plugin loads, runs a hook, and its crash does not bring down the application | passed | "example plugin: all hooks run…", "bad plugins are reported and never stop loading" (crashing plugin isolated), GUI `plugins_on_worker_thread`. |
| 6 | Damaged binary: no crash, friendly error, loads what it can | passed | C++ "corrupted ELF header falls back to raw", "truncated PE reports but loads", fuzzing (mutations, truncation, random data), "load errors"; GUI: empty file → friendly error. |
| 7 | Tutorial "first analysis in 10 minutes" (walkthrough) | passed (document); with a real beginner **could not be verified** | `docs/quickstart.md`, `docs/tutorials.md`. The steps match the GUI test. |
| 8 | `reversa analyze file.exe --report report.md` headless in CI | passed | "cli: analyze writes Markdown, HTML, JSON and a project", `headless_check` (also `.exe` samples) in CI on Linux and Windows. |
| 9 | License check in CI, LICENSE-THIRD-PARTY complete | passed | Job `licenses` in CI. |
| 10 | All tests green in CI, release build + packages for 2 platforms | passed | [CI log](#ci-log): release build, `.deb` and Windows zip as CI artifacts. |

## MUST-HAVE features (4.1)

| # | Feature | State | Evidence |
|---|---|---|---|
| 1 | ELF, PE, raw data with a manual architecture | passed | C++ "load all samples", "raw shellcode analysis", "cli: raw firmware with a manual architecture" |
| 2 | Detecting architecture, endianness, bitness, compiler | passed | C++ "headers of the hello samples", "format detection and raw arch guess", "PE details" |
| 3 | Disassembler with coloured syntax and clickable operands | passed | C++ "x86-64 flow and references", "arm64 adrp tracking and flow"; GUI listing with colours and links (the GUI test clicks through links, screenshots with `REVERSA_SHOTS`). |
| 4 | Functions: name, address, size, number of calls/x-refs | passed | Functions panel (Name, Address, Callers, Size); C++ "CFG loops, call graph neighbourhood, function_containing" |
| 5 | ASCII and UTF-16 strings with references | passed | "string, data pointer and read xrefs", golden facts; Strings tab (References column) |
| 6 | Imports, exports, relocations, sections with a hex dump | passed | C++ "ELF details", "PE details"; GUI tabs Imports, Exports, Sections and hex, *Advanced → Relocations* |
| 7 | X-refs: called from, read from, written from | passed | C++ "string, data pointer and read xrefs", "x86 memory access kinds…"; Details panel |
| 8 | Call graph (filter, search, zoom) | passed | GUI Call graph tab (depth, imports, wheel zoom, double click); C++ call graph test |
| 9 | CFG with highlighted loops | passed | C++ "CFG loops…"; GUI: loops in orange, back edges dashed |
| 10 | Pseudocode: calls, branches, loops, variables | passed | C++ decompiler tests + golden pseudocode of all 10 samples |
| 11 | Renaming functions and variables with propagation, undo/redo | passed | C++ rename tests; GUI test (function and variable, Undo/Redo) |
| 12 | Comments and bookmarks | passed | "C ABI host…" (comments, bookmarks, undo); GUI test; Bookmarks tab |
| 13 | Search: strings, bytes, hex patterns, instructions, regex in pseudocode | passed | C++ "search: bytes, strings, instructions, pseudocode, errors"; "cli: demo, info, search"; GUI search and *Advanced* searches |
| 14 | API for scripts + 3 example scripts | passed | C/C++ API (`sdk/include/reversa`), scripts `strcmp_callers`, `function_table`, `annotate_and_view` in `examples/scripts`; tests "scripts: run…", "C++ SDK wraps the host…", `docs/scripting-tutorial.md`. The Python API was replaced by C++ at the user's request. |
| 15 | Saving the project | passed | C++ "project roundtrip…", "project migrations…", "projects written by REVERSA 0.1 (Python) still open", "project is plain SQLite…" |
| 16 | Markdown/HTML report | passed | "cli: analyze writes Markdown, HTML, JSON and a project"; GUI *File → Export report* |
| 17 | Plugins: folders, entry point, hooks, API version | passed | `tests/cpp/test_plugins.cpp` (plugin in C++ and in C, broken libraries, other API version, failed hook, `REVERSA_PLUGIN_PATH`), GUI `plugins_on_worker_thread`, *Advanced → Plugins* |
| 18 | Undo/redo in the whole GUI | passed | GUI test: rename, variable, comment, bookmark undone with the Undo command, then Redo |
| 19 | Dark/light theme, custom shortcuts | passed | GUI test: theme switch, saving a custom shortcut (`Ctrl+R` for Rename) and using it in a new window |
| 20 | Stable import of damaged files | passed | fuzz tests (3), "load errors", "project save errors…", damaged project in "project roundtrip…" |

## Security boundaries

| Boundary | State | Evidence |
|---|---|---|
| The analysed file is never run | passed | The code does not start processes on the analysed file (`src/` contains no `system`, `popen`, `CreateProcess`). |
| No telemetry, no network | passed | `src/` uses no network; the GUI is native and listens on no port (the web server of 0.1 was removed). |
| No features to bypass DRM, licenses, anti-cheat | passed | [docs/safety.md](safety.md#ethics). No such feature exists. |
| Locked dependencies and license check | passed | Pinned git tag (Capstone), vcpkg baseline, Actions pinned to commits, job `licenses`. Tests and tools need no extra packages. |
| Plugins and scripts are sandboxed | **failed** (known limitation) | Error isolation yes, sandbox no. Documented in plugin-tutorial.md, safety.md and SECURITY.md. Roadmap. |

## Deviations from the specification

- No debugger (NICE-TO-HAVE, item 22).
- The specification asked for Slovak documentation; at the user's request the whole project
  (code, GUI, documentation) is in English. The GUI is also available in Slovak, German, French
  and Chinese.
- The web GUI of version 0.1 was replaced by a native Qt GUI (the user asked for the code to be in
  C++). Scripts and plugins are also in C/C++; Python scripts and plugins from 0.1 no longer work.
- REA could not do a full analysis (no Ghidra, downloads blocked). See [research.md](research.md).

## Known limitations

See [architecture.md](architecture.md#known-limitations). None of them is critical for the
acceptance criteria.

## CI log

Run [45052061572](https://github.com/MrKniha/reversa/actions/runs/45052061572) on branch `cpp`
(22 Nov 2026): **all jobs green**. Later runs on `main` (Python removed, English everywhere) are
listed on the [Actions page](https://github.com/MrKniha/reversa/actions/workflows/ci.yml).

| Job | Result |
|---|---|
| licenses | passed |
| Linux (Ubuntu 24.04) | ctest 2/2 (42 C++ tests + GUI test), pytest 80/80, headless 10 samples, `.deb` (2 MB) installed and run |
| Windows (Server 2022, MSVC) | ctest 2/2, pytest 80/80, headless 10 samples, zip (27 MB) tested with a clean `PATH` |

Problems found in CI and fixed: missing `#include` for MSVC, the Python DLL cannot be linked with
`/DELAYLOAD`, CRLF in golden files (added `.gitattributes`), the file name in reports with Windows
paths, a CLI test started the GUI, the `max` macro from `windows.h` (`NOMINMAX`).

## Progress log

| Date | Step |
|---|---|
| 2026-11-22 | Version 0.1 in Python (see the git history): research, core, decompiler, web GUI, CI. |
| 2026-11-22 | User decision: all code in C++. Rewrite of the core (formats, disassembler, analysis, decompiler), with outputs checked against 0.1 on all samples. |
| 2026-11-22 | Session, SQLite projects (compatible with 0.1), reports, CLI with the same commands and outputs. |
| 2026-11-22 | Python API through pybind11, embedded interpreter for `reversa script` and plugins; tests of 0.1 moved to C++ and `tests/python`. |
| 2026-11-22 | Qt GUI, GUI test, packages (.deb, Windows zip), CI for Linux and Windows, documentation. |
| 2026-11-22 | GUI languages (English, Slovak, German, French, Chinese), crayfish logo, English CLI and reports. |
| 2026-11-22 | Python removed: C/C++ plugins and scripts over a stable C ABI (`sdk/include/reversa`), tests and repository tools in C++, Windows zip through CPack. |
| 2026-11-22 | Whole project in English: GUI source language, documentation, README. |
