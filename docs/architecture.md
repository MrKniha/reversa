# REVERSA architecture

How REVERSA is built, why, and what comes next. Terms are explained in the [glossary](glossary.md).

## Layers

```
┌──────────────────────────────────────────────────────────────────────┐
│ Interfaces: Qt GUI (src/gui)   CLI (src/cli)   reports (src/report)   │
│             C/C++ plugins and scripts (src/plugins, sdk/include)      │
├──────────────────────────────────────────────────────────────────────┤
│ Services:   Session (src/session): open, analyse, annotations,        │
│             undo/redo, pseudocode cache, project saving               │
│             EventBus and UndoStack (src/session/events)               │
├──────────────────────────────────────────────────────────────────────┤
│ Analysis:   src/analysis: functions, blocks, x-refs, strings, graphs, │
│             search        src/decompiler: IR, lifting, passes,        │
│             structuring, renderer                                     │
├──────────────────────────────────────────────────────────────────────┤
│ Core:       src/formats: ELF, PE, raw data (own parsers)              │
│             src/disasm: Capstone   src/core: Binary, JSON, text       │
│             src/project: SQLite project (.rvproj) with migrations     │
└──────────────────────────────────────────────────────────────────────┘
```

Each layer uses only the layers below it. The GUI, the CLI and plugins all call the same
`rv::Session`, so they behave the same. A rename from a script, for example, shows up in the GUI
at once through the EventBus.

## Modules

| Module | Responsibility |
|---|---|
| `src/core` | `Binary`, `Section`, `Symbol`, `Import`, `Export`, `Relocation`, `Diagnostic`; `Reader` with bounds checks; errors (`ReversaError` → `LoadError`, `ProjectError`, `AnnotationError`, `Cancelled`); own `Json`; SHA-256; UTF-8 helpers. |
| `src/formats` | ELF, PE and raw data parsers, format detection, architecture guess. A damaged part becomes a diagnostic, the rest is loaded. |
| `src/disasm` | Turns Capstone instructions into `Insn` (control flow, target, memory references). Tracks `adrp`+`add` on ARM64. |
| `src/analysis` | Recursive descent from entry points, symbols, exports and `.pdata`. Noreturn functions, PLT/IAT thunks, finding `main`, x-refs, ASCII and UTF-16 strings, CFG, dominators, loops, call graph, search. |
| `src/decompiler` | `ir` (expressions and statements), `lift` (x86, x86-64, AArch64), `passes` (propagation, DCE), `structure` (if/else, while, do-while, goto), `render` (segments with a kind and a reference). |
| `src/project` | Annotations and SQLite storage with schema v1 and migrations. The format is compatible with projects from version 0.1 (Python). |
| `src/session` | `Session`: names, `resolve`, edits through the `UndoStack`, events, plugin views, saving. |
| `src/report` | Markdown and HTML reports and a JSON summary. |
| `src/cli` | Command line (`analyze`, `info`, `search`, `script`, `gui`, `--demo`). |
| `src/plugins` | Loads native plugins and scripts (`dlopen` / `LoadLibraryW`), the `rv_host` function table, isolation of failing plugins. |
| `sdk/include/reversa` | Public SDK: `plugin.h` (stable C ABI, `RV_API_VERSION`) and `sdk.hpp` (C++ helper: `Program`, `Plugin`, macros `REVERSA_PLUGIN`, `REVERSA_SCRIPT`). |
| `src/gui` | Qt Widgets: main window, views (listing, pseudocode, details, hex), graph (`GraphView`), dialogs (guide, settings). English is the source language; translations are in `src/gui/i18n/*.json`. |

## Data flows

**Opening a file:**
`file → detect_format → ELF/PE/RAW parser → Binary (+ diagnostics)
→ plugins on_load → analyze() on a worker thread (progress, cancel)
→ Analysis → plugins on_function_analyzed → EventBus "analysis_done" → GUI / CLI`.

**Pseudocode:**
`function → build_cfg → lifter (Insn → IR) → passes → structuring
→ renderer (segments) → Session cache → GUI (clickable names and variables) / text`.
A rename invalidates the cache, so the new name shows up everywhere.

**Rename:**
`GUI/API → Session::rename → validation → Command on the UndoStack
→ Annotations → EventBus "renamed" → plugins on_rename → GUI refreshes its views`.

**Saving a project:**
`Session::save → save_project → temporary file → atomic rename`.
The project contains the SHA-256 of the binary. When it is opened, REVERSA checks that the binary
matches.

## Why C++ and Qt

Version 0.1 was written in Python with a web GUI in the browser. Since version 0.2 the whole tool
is C++20 with a native Qt 6 interface, including scripts and plugins.

- Speed and memory. Analysis, x-refs and the decompiler run natively, without an interpreter.
  Large binaries (tens of thousands of functions) are realistic, and the data model is compact
  (`std::map`, `unordered_map`, no extra objects per instruction).
- One program without dependencies. The Windows package is a zip with `reversa-gui.exe`; the
  Linux package is a `.deb` with system libraries. You do not need Python or a browser.
- Native GUI. Cutter and other tools already use Qt Widgets. It has dockable panels, keyboard
  shortcuts and a dark theme, and works the same on Windows and Linux. Without a local HTTP server
  there is no HTTP attack surface.
- C/C++ plugins and scripts over a stable C ABI. A plugin or a script is a shared library that
  talks to REVERSA only through the `rv_host` function table (`sdk/include/reversa/plugin.h`). It
  does not link against REVERSA, works with any C or C++ compiler, and old plugins keep working in
  newer versions: functions are only appended to the table, and an incompatible change raises
  `RV_API_VERSION`. Python plugins and scripts from version 0.1 are no longer supported (decision
  of 2026-11-22: the whole tool in C++).
- Licenses. Qt is LGPL-3.0 and we link it dynamically (replaceable libraries), which is fine
  with REVERSA's MIT license. Capstone is BSD-3-Clause, SQLite is public domain.

The downside is a harder build (CMake, a compiler, Qt). CI therefore builds packages for Windows and
Linux, and [building.md](building.md) describes the build step by step.

## Other decisions

**Capstone.** One disassembler for x86, x86-64, ARM, AArch64 and other architectures, BSD-3
license. Zydis and iced are more accurate for x86, but other architectures would need another
library. We link it statically, with only the four architectures we use.

**Own ELF and PE parsers.** Malware often has damaged or deliberately broken headers. Our parsers
check bounds on every read, load what they can and report the rest as diagnostics. Fuzz tests (`tests/cpp/test_analysis.cpp`) check this.

**Own graph layout.** Function graphs usually have tens of blocks. A simplified layered algorithm
(after dagre and Sugiyama) in `QGraphicsView` is enough for that. Large graphs (hundreds of blocks)
will need a better algorithm (roadmap).

**SQLite for projects.** One file, atomic transactions, a schema version and migrations. JSON would
be simpler, but slower for large projects and without transactions.

**Intermediate representation (IR) in the decompiler.** A lifter per architecture turns
instructions into a common IR. Optimisations and structuring are then shared. You add a new
architecture by writing one lifter.

**Locked dependencies.** CMake downloads Capstone at a pinned git tag, SQLite on Windows comes from
vcpkg with a pinned `builtin-baseline`, and GitHub Actions are pinned to commits. Tests and
repository tools (`tools/*.cpp`) need nothing extra. `tools/check_licenses.cpp` checks the license
of every dependency in CI.

**MIT license.** The simplest for users and companies, compatible with all our dependencies. GPL
tools (Unicorn, Keystone) therefore cannot be a required part, only optional plugins.

**Plugin isolation without a sandbox.** A hook that returns an error (the `REVERSA_PLUGIN` macro
also turns an exception into one) disables the plugin and REVERSA keeps running. A library that
cannot be loaded or has a different API version is skipped and reported. A memory crash in a
native plugin, however, ends the whole application. During the analysis, hooks run on the worker
thread, and calls to one program are serialised. A real sandbox (a separate process) is on the
roadmap. Until then the documentation warns that plugins are trusted code.

## What we learned from existing projects

- **Ghidra**: separate the IR from the architecture (P-code). Show pseudocode and listing side by
  side and keep them in sync.
- **Cutter and IDA**: one "current address" drives all views. Layout: functions on the left,
  content in the centre, details on the right.
- **radare2 and Rizin**: short keyboard shortcuts. A project as a separate file with a format
  version.
- **angr**: recursive descent and a list of noreturn functions find more than prologue
  heuristics. A complete API as a first-class interface.
- **Detect It Easy**: showing the compiler and packer right after a file opens helps beginners.
- **RetDec**: mark decompiler output as an estimate.
- **binwalk**: firmware is often just raw data; the architecture and base address must be given by
  hand.
- **LIEF, pyelftools, pefile**: one binary model for all formats. We also saw that libraries that
  throw an exception on an error are not suitable for damaged files.

## Security model

- Static analysis only. The analysed file is never run.
- No network, no telemetry. The GUI is native and listens on no port.
- Every read from the file goes through bounds checks; fuzz tests run in CI.
- Plugins and scripts are not sandboxed (see above). They are loaded only from the user's plugin
  folder, `REVERSA_PLUGIN_PATH` and folders given with `--plugins`; `--no-plugins` turns them off.

## Priorities (MVP, section 4.2)

| Priority | Feature | State in 0.2.0 |
|---|---|---|
| P0 | Loading ELF/PE, robust against damage | done |
| P0 | Disassembly x86, x86-64, ARM64 (ARM 32-bit basics) | done |
| P0 | Functions, strings, imports, exports, x-refs | done |
| P0 | CFG and call graph | done |
| P0 | Pseudocode (provisional) | done |
| P0 | Rename, comments, bookmarks, undo/redo, project | done |
| P0 | API for scripts, plugins with isolation (C/C++) | done |
| P0 | Headless CLI, reports | done |
| P1 | English and Slovak interface | done (also German, French, Chinese) |
| P1 | Onboarding, help, glossary | done |

## Roadmap (section 4.3, outside the MVP)

1. Mach-O and APK/DEX (now they load only as raw data).
2. Jump tables (`switch`), types and structures, library signatures (FLIRT-like).
3. A better decompiler: SSA, type inference, floating point (now `__asm`).
4. A sandbox for plugins (separate process).
5. Diffing two versions of a binary.
6. Emulation (optional plugin with Unicorn, GPL-2.0).
7. Patch mode: changing your own binaries to fix bugs and for teaching. It will never contain
   features to bypass DRM, licenses, anti-cheat or copy protection.
8. A debugger (local, sandboxed), collaboration on a project.

## Known limitations

- `main` is not found in a stripped PE from MinGW (there is no pattern like `__libc_start_main`).
- Jump tables are not resolved; indirect jumps end a block.
- Floating point and SIMD instructions appear in the pseudocode as `__asm(...)`.
- Plugins are not sandboxed.
- Mach-O, APK and other formats load as raw data.
- The GUI listing shows one function at a time; data outside functions is shown by the hex dump on
  the Sections and hex tab.
