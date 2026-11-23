# Security policy

## Reporting vulnerabilities

Report vulnerabilities in REVERSA (for example a crash, an out-of-bounds read or code execution
when a malicious binary or project is opened) privately through the repository's GitHub
Security Advisories ("Report a vulnerability"), not in a public issue. We reply within 7 days.

Include the version (`reversa --version`), your OS, steps to reproduce and, if it is safe, a sample
(in a ZIP archive with the password `infected`).

## Threat model (MVP)

- REVERSA never runs the analysed code. The analysis is static only.
- The format parsers are written in C++ and every read from the file goes through `Reader`, which
  checks bounds. They are fuzzed in CI (`tests/cpp/test_analysis.cpp`, longer run:
  `REVERSA_FUZZ_ITERS`). The Capstone disassembler is a native C library.
- The GUI is native (Qt) and listens on no network port. REVERSA has no telemetry.
- Plugins and scripts are native libraries (.so/.dll) without a sandbox. They run inside the
  REVERSA process with your permissions. Only run the ones you trust. REVERSA loads them only from
  `~/.reversa/plugins`, from `REVERSA_PLUGIN_PATH` and from folders given with `--plugins`.
- A project file `.rvproj` contains a copy of the analysed binary. If you analyse malware, treat
  the project like a malware sample.
