# Contributing to REVERSA

Thank you for helping! The rules are short:

1. **Issue first.** Describe a larger change in an issue first, so we agree on the direction.
2. **Everything in English.** Code, comments, CLI, error messages, reports and documentation are
   in English. GUI texts are English and always wrapped in `tr(...)` (outside classes `T(...)`,
   in static tables `QT_TR_NOOP(...)`). Translations live in `src/gui/i18n/<language>.json`
   (sk, de, fr, zh). In CI, `build/bin/i18n_check .` checks that no text is missing, and
   `build/bin/i18n_check . missing` prints what still needs translating.
3. **Tests are required.** Every bug fix comes with a regression test, every new feature with at
   least a unit test.
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
   ctest --test-dir build --output-on-failure          # C++ tests (API, plugins, CLI, golden) + GUI test
   build/bin/headless_check build/bin/reversa .
   ```
   Longer fuzzing: `REVERSA_FUZZ_ITERS=5000 build/bin/reversa_tests`.
4. **No new dependencies without discussion.** Every dependency needs an MIT-compatible license
   and a pinned version (a git tag in `FetchContent` or the `vcpkg.json` baseline). It must be
   listed in `LICENSE-THIRD-PARTY` and allowed in `tools/check_licenses.cpp`.
5. **No binaries without source.** Add test binaries only together with their source and build
   script (`samples/build.sh`). Malware does not belong in the repository. The icons
   `assets/reversa-*.png` and `assets/reversa.ico` are rendered from `assets/reversa.svg`; render
   them again after changing the logo.
6. **Ethics.** We do not accept features meant to bypass DRM, license checks, activation or
   anti-cheat (see [docs/safety.md](docs/safety.md#ethics)).
7. **Golden files** (`samples/expected/*.json`, `tests/cpp/golden/*.pseudo.txt`) change only on
   purpose: `build/bin/rvdump --expected samples` or
   `build/bin/rvdump --pseudo samples/bin/X > tests/cpp/golden/X.pseudo.txt`. Explain in the PR
   why the result changed.

Style: C++20, lines up to 120 characters, `namespace rv`, errors as `ReversaError` with a clear
message and a technical detail. The plugin interface `sdk/include/reversa/plugin.h` is a stable
C ABI: only append new functions to the end of `rv_host` and never change existing ones; a change
that would break old plugins must raise `RV_API_VERSION`.
