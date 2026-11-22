# Research and references

This document records what we based the design of REVERSA on, which tools we used during the
research and what did (not) work. The repository data was collected on 22 Nov 2026 through the
GitHub API (`gh api repos/<owner>/<repo>`). Stars and the date of the last push change, so treat
them as approximate.

**No code from these projects is copied into REVERSA.** We took only ideas, conventions and proven
practices (see the column "what we took"). The only direct dependency among them is Capstone
(BSD-3-Clause). See [LICENSE-THIRD-PARTY](../LICENSE-THIRD-PARTY).

## Projects

| Project | URL | License (per GitHub) | ★ | last push | What we took |
|---|---|---|---|---|---|
| Ghidra | https://github.com/NationalSecurityAgency/ghidra | Apache-2.0 | 100 942 | 2026-11-20 | The split into listing / decompiler / graph. We changed the naming `FUN_`/`DAT_` to `sub_`/`s_`. The P-code idea: an intermediate representation (IR) between assembly and pseudocode. The extension manifest (`extension.properties`) as a model for plugin metadata. |
| radare2 | https://github.com/radareorg/radare2 | LGPL-3.0 (GitHub: NOASSERTION) | 25 483 | 2026-11-22 | Short keyboard shortcuts and commands. We used it as an **independent check** of our results (see below). |
| Rizin | https://github.com/rizinorg/rizin | LGPL-3.0 | 4 220 | 2026-11-22 | Projects as a separate file with a schema version. |
| Cutter | https://github.com/rizinorg/cutter | GPL-3.0 | 20 250 | 2026-11-13 | GUI layout: functions on the left, tabs in the centre, details on the right. Views kept in sync by one "current address". |
| angr | https://github.com/angr/angr | BSD-2-Clause | 9 406 | 2026-11-20 | Recursive descent for finding functions, detecting noreturn functions, the CFG structure. A complete API as a first-class interface. |
| Capstone | https://github.com/capstone-engine/capstone | BSD-3-Clause | 9 204 | 2026-11-21 | **Dependency**: disassembler for x86, x86-64, ARM, AArch64. |
| Unicorn | https://github.com/unicorn-engine/unicorn | GPL-2.0 | 9 601 | 2026-11-10 | Not used in the MVP (emulation is on the roadmap). Because of GPL-2.0 it could only be an optional plugin. |
| Frida | https://github.com/frida/frida | wxWindows Library Licence (GitHub: NOASSERTION) | 22 892 | 2026-11-20 | Not used. Dynamic analysis is outside the MVP. |
| pwndbg | https://github.com/pwndbg/pwndbg | MIT | 11 238 | 2026-11-19 | Clear display of context (registers, stack) as inspiration for a future debugger view. |
| Detect It Easy | https://github.com/horsicq/Detect-It-Easy | MIT | 11 991 | 2026-11-22 | Detecting the compiler and packers (UPX) from sections and strings. |
| binwalk | https://github.com/ReFirmLabs/binwalk | MIT | 14 734 | 2026-08-11 | Raw data (firmware) as a separate format with a manual architecture and base address. |
| LIEF | https://github.com/lief-project/LIEF | Apache-2.0 | 5 678 | 2026-11-17 | One binary model (sections, symbols, imports, exports, relocations) for ELF and PE. |
| RetDec | https://github.com/avast/retdec | MIT | 8 674 | 2026-05-26 | Decompilation stages (lifting, optimisations, structuring). Marking the output as an estimate. |
| pyelftools | https://github.com/eliben/pyelftools | Public domain / Unlicense (GitHub: NOASSERTION) | 2 290 | 2026-11-02 | Comparison with our own ELF parser during development. |
| pefile | https://github.com/erocarrera/pefile | MIT | 2 090 | 2026-11-22 | Comparison with our own PE parser during development (imports, exports, relocations). |
| Zydis | https://github.com/zyantific/zydis | MIT | 4 438 | 2026-11-13 | Alternative to Capstone for x86. Not taken (x86 only). |
| iced | https://github.com/icedland/iced | MIT | 3 634 | 2026-11-22 | Alternative to Capstone for x86. Not taken (x86 only). |
| Keystone | https://github.com/keystone-engine/keystone | GPL-2.0 | 2 663 | 2026-11-08 | Not used. Assembler for the patch mode (roadmap); GPL-2.0, so optional only. |
| Cytoscape.js | https://github.com/cytoscape/cytoscape.js | MIT | 11 300 | 2026-11-10 | Not used. Considered for graphs, replaced by our own layout. |
| dagre | https://github.com/dagrejs/dagre | MIT | 5 863 | 2026-10-24 | Layered layout algorithm (Sugiyama): layers by the longest path, fewer crossings. We wrote our own simplified version. |
| PySide6 | https://github.com/pyside/pyside-setup | LGPL-3.0 / GPL (as Qt) | 138 (mirror) | 2026-11-20 | Considered for the GUI of version 0.1. Not used (size, LGPL conditions when distributing). |

## Comparing key libraries

These are the decisions of version 0.1 (Python). Version 0.2 kept Capstone, the own parsers and
SQLite, and replaced the web GUI with a native Qt GUI and the SVG graphs with `QGraphicsView`
(see [architecture.md](architecture.md#why-c-and-qt)).

| Decision | Options | Choice | Reason |
|---|---|---|---|
| Disassembler | Capstone, Zydis, iced, own | **Capstone** | More architectures (x86, ARM, AArch64, MIPS…), BSD-3, ready-made packages for Linux, Windows and macOS. Zydis and iced are better for x86 but support nothing else. |
| ELF/PE parsers | pyelftools, pefile, LIEF, own | **own** | We needed robustness against damaged files: every error is a diagnostic, not an exception. No extra dependencies. We used pyelftools and pefile only for comparison during development. |
| GUI (0.1) | PySide6/Qt, Tauri, Electron, local web | **local web (stdlib HTTP server + vanilla JS)** | No extra dependencies, the same on all OSes, easy testing (Playwright). Replaced by Qt in 0.2. |
| Graphs (0.1) | Cytoscape.js, dagre, Graphviz, own | **own SVG** | Small function graphs (tens of blocks), no CDN and no npm build step. Replaced by `QGraphicsView` in 0.2. |
| Project | JSON, SQLite | **SQLite** | Atomic saving, schema version, migrations; available everywhere. |

## Log of using REA (`npx rea-agents`)

The specification recommends REA. Result: **partly working**.

| Step | Command | Result |
|---|---|---|
| Install | `npx -y rea-agents --help` | Works, version `rea@6.10.1`. |
| Diagnostics | `npx rea-agents doctor`, `capabilities` | Works. Native analyses need an external provider (Ghidra, Hopper, IDA or kuna). |
| Binary analysis | `npx rea-agents analyze /usr/bin/true` | **Does not work**: `code: provider_unavailable`, "Set GHIDRA_INSTALL_DIR to an extracted Ghidra 12.x (>=12.2) release directory." |
| Downloading Ghidra | downloading a release from github.com | **Does not work**: the environment's proxy returned HTTP 403 for release files. |
| Artifact inspection | `npx rea-agents inspect-artifact libcapstone.so` and `… login_x64.elf` | Works: ELF format, SHA-256 (`a0897f80…dc84` for libcapstone.so), an inventory without running the file. |
| Release audit | `npx rea-agents audit-release-exposure libcapstone.so` | Works: 15 checks of what static analysis reveals (for example source maps, debug files next to the binary). |

Because a full analysis through REA was not possible, we continued as instructed through GitHub
(metadata and documentation of the projects above) and an independent check with radare2.

## Checking the Capstone dependency

Before using `libcapstone.so` from the Python package `capstone 5.0.9`, we checked it statically
(`rabin2 -I`, `rabin2 -i`, radare2 5.5.0):

- x86-64, PIC, NX enabled, not stripped, partial RELRO, no stack canary.
- Imports only from libc (malloc, free, memcpy, printf, strcmp…). **No network functions**
  (socket, connect, getaddrinfo) and no process creation.
- The BSD-3-Clause license is compatible with MIT.

## Independent check of the analysis results (radare2)

On the sample binaries we compared the results of REVERSA with radare2 5.5.0
(`aaa; axt @ sym.imp.strcmp; afl`):

| File | Callers of `strcmp` (radare2) | Callers of `strcmp` (REVERSA) | Match |
|---|---|---|---|
| `login_x64.elf` | check_password, check_user | check_password, check_user | yes |
| `login_x64_stripped.elf` | 0x401196, 0x401217 | sub_401196, sub_401217 | yes |
| `login_arm64.elf` | check_password, check_user | check_password, check_user | yes |
| `login_x64.exe` (MinGW PE) | radare2 with `axt @ sym.imp.strcmp` found nothing (different import name) | check_password, check_user | cannot compare |

Both tools found the functions `main`, `sum_to`, `classify`, `greet` in `hello_x64.elf` at the same
addresses. The expected values ("golden files") are in `samples/expected/` and
`tests/cpp/test_golden.cpp` checks them.

## What we refused

In line with the specification (section 7), REVERSA does not and will not contain features to
bypass DRM, license checks, anti-cheat systems or copy protection. It also contains no telemetry
and no cloud services.
