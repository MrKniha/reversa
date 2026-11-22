# Bezpečnostná politika

## Hlásenie zraniteľností

Zraniteľnosti v REVERSA (napr. pád alebo spustenie kódu pri otvorení škodlivej binárky,
obídenie tokenu lokálneho GUI servera) hláste **súkromne** cez GitHub Security Advisories
repozitára („Report a vulnerability“), nie verejným issue. Odpovieme do 7 dní.

Uveďte verziu (`reversa --version`), OS, kroky na reprodukciu a ak je to bezpečné, vzorku
(zabalenú v ZIP s heslom `infected`).

## Model hrozieb (MVP)

- REVERSA **nikdy nespúšťa** analyzovaný kód – analýza je čisto statická.
- Parsery formátov sú v Pythone (pamäťovo bezpečné) a sú fuzzované (`tests/test_fuzz.py`).
  Disassembler Capstone je natívna knižnica v C.
- GUI server počúva iba na `127.0.0.1`, vyžaduje náhodný token pri každom API volaní a
  odmieta cudzie hlavičky `Host` (ochrana proti DNS rebinding).
- **Pluginy a skripty sú bežný Python kód bez sandboxu** – spúšťajte len tie, ktorým dôverujete.
- Projektový súbor `.rvproj` obsahuje kópiu analyzovanej binárky. Ak analyzujete malvér,
  zaobchádzajte s projektom ako so vzorkou malvéru.
