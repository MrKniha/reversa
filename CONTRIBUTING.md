# Ako prispieť do REVERSA

Ďakujeme, že chcete pomôcť! Pravidlá sú krátke:

1. **Najprv issue.** Väčšiu zmenu najprv popíšte v issue, nech sa zhodneme na smere.
2. **Kód po anglicky, dokumentácia po slovensky.** Identifikátory a komentáre v kóde píšeme
   anglicky; README a `docs/` slovensky. Nové texty v UI patria do `src/reversa/ui/i18n/sk.json`
   (a ideálne aj `en.json`) – nie priamo do JavaScriptu.
3. **Testy sú povinné.** Každá oprava chyby má regresný test, každá nová funkcia aspoň unit test.
   ```bash
   pip install -e ".[dev]"
   pytest                     # všetky testy
   coverage run -m pytest && coverage report   # pokrytie (cieľ >= 80 %)
   node tests/ui/smoke.mjs    # UI smoke test (vyžaduje Playwright + Chromium)
   ```
4. **Žiadne nové závislosti bez diskusie.** Každá závislosť musí mať licenciu kompatibilnú s MIT,
   musí byť zapísaná v `LICENSE-THIRD-PARTY`, `requirements.lock` a povolená v `scripts/check_licenses.py`.
5. **Žiadne binárky bez zdroja.** Testovacie binárky pridávajte len so zdrojákom a build skriptom
   (`samples/build.sh`). Malvér do repozitára nepatrí.
6. **Etika.** Funkcie určené na obchádzanie DRM, licenčných kontrol, aktivácií alebo anti-cheatu
   neprijmeme (pozri README, kapitola Etika).
7. **Golden súbory** (`samples/expected/*.json`) meňte len vedome: `python samples/make_expected.py`
   a v PR vysvetlite, prečo sa výsledok zmenil.

Štýl: PEP 8, riadky do 120 znakov, typové anotácie vo verejnom API.
