# Bezpečnostná politika

## Hlásenie zraniteľností

Zraniteľnosti v REVERSA (napr. pád, čítanie mimo pamäte alebo spustenie kódu pri otvorení
škodlivej binárky či projektu) hláste **súkromne** cez GitHub Security Advisories
repozitára („Report a vulnerability“), nie verejným issue. Odpovieme do 7 dní.

Uveďte verziu (`reversa --version`), OS, kroky na reprodukciu a ak je to bezpečné, vzorku
(zabalenú v ZIP s heslom `infected`).

## Model hrozieb (MVP)

- REVERSA **nikdy nespúšťa** analyzovaný kód – analýza je čisto statická.
- Parsery formátov sú v C++ a každé čítanie zo súboru ide cez `Reader` s kontrolou hraníc.
  Sú fuzzované v CI (`tests/cpp/test_analysis.cpp`, dlhší beh: `REVERSA_FUZZ_ITERS`).
  Disassembler Capstone je natívna knižnica v C.
- GUI je natívne (Qt), nepočúva na žiadnom sieťovom porte. REVERSA nemá telemetriu.
- **Pluginy a skripty sú bežný Python kód bez sandboxu** – spúšťajte len tie, ktorým dôverujete.
- Projektový súbor `.rvproj` obsahuje kópiu analyzovanej binárky. Ak analyzujete malvér,
  zaobchádzajte s projektom ako so vzorkou malvéru.
