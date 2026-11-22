# Ako prispieť do REVERSA

Ďakujeme, že chcete pomôcť! Pravidlá sú krátke:

1. **Najprv issue.** Väčšiu zmenu najprv popíšte v issue, nech sa zhodneme na smere.
2. **Kód, CLI, chybové hlásenia a reporty po anglicky; dokumentácia po slovensky.** Identifikátory a komentáre v kóde píšeme
   anglicky; README a `docs/` slovensky. Texty v GUI sú slovenské a vždy obalené v `tr(...)`
   (mimo tried `T(...)`, v statických tabuľkách `QT_TR_NOOP(...)`). Preklady sú v
   `src/gui/i18n/<jazyk>.json` (en, de, fr, zh); `build/bin/i18n_check .` v CI overí, že
   žiadny text nechýba, a `build/bin/i18n_check . missing` vypíše, čo treba doplniť.
3. **Testy sú povinné.** Každá oprava chyby má regresný test, každá nová funkcia aspoň unit test.
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
   ctest --test-dir build --output-on-failure          # C++ testy (API, pluginy, CLI, golden) + GUI test
   build/bin/headless_check build/bin/reversa .
   ```
   Dlhší fuzzing: `REVERSA_FUZZ_ITERS=5000 build/bin/reversa_tests`.
4. **Žiadne nové závislosti bez diskusie.** Každá závislosť musí mať licenciu kompatibilnú s MIT,
   pevnú verziu (git tag vo `FetchContent` alebo `vcpkg.json` baseline),
   musí byť zapísaná v `LICENSE-THIRD-PARTY` a povolená v `tools/check_licenses.cpp`.
5. **Žiadne binárky bez zdroja.** Testovacie binárky pridávajte len so zdrojákom a build skriptom
   (`samples/build.sh`). Malvér do repozitára nepatrí. Ikony `assets/reversa-*.png`
   a `assets/reversa.ico` sú vyrenderované z `assets/reversa.svg`; po zmene loga ich vyrenderujte znova.
6. **Etika.** Funkcie určené na obchádzanie DRM, licenčných kontrol, aktivácií alebo anti-cheatu
   neprijmeme (pozri [docs/safety.md](docs/safety.md#etika)).
7. **Golden súbory** (`samples/expected/*.json`, `tests/cpp/golden/*.pseudo.txt`) meňte len vedome:
   `build/bin/rvdump --expected samples`, resp. `build/bin/rvdump --pseudo samples/bin/X > tests/cpp/golden/X.pseudo.txt`,
   a v PR vysvetlite, prečo sa výsledok zmenil.

Štýl: C++20, riadky do 120 znakov, `namespace rv`, chyby ako `ReversaError` so zrozumiteľnou
anglickou správou a technickým detailom. Rozhranie pluginov `sdk/include/reversa/plugin.h` je
stabilné C ABI: nové funkcie pridávajte len na koniec `rv_host`, nikdy nemeňte existujúce; zmena,
ktorá by staré pluginy rozbila, musí zvýšiť `RV_API_VERSION`.
