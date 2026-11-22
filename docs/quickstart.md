# Quick start za 10 minút

Cieľ: nainštalovať REVERSA, otvoriť ukážkovú binárku a nájsť v nej heslo – bez predchádzajúcich znalostí.

## 1. Inštalácia (2 minúty)

Potrebujete Python 3.10 alebo novší.

```bash
git clone https://github.com/MrKniha/reversa.git && cd reversa
python3 -m venv .venv && . .venv/bin/activate     # Windows: .venv\Scripts\activate
pip install .
reversa --version
```

## 2. Ukážková analýza (1 minúta)

```bash
reversa --demo
```

Otvorí sa prehliadač s REVERSA (beží iba na vašom počítači, adresa `127.0.0.1`). Pri prvom spustení uvidíte
krátkeho sprievodcu – môžete ho preskočiť. Bez grafického prostredia (server, CI) použite
`reversa --demo --no-gui`: výsledky sa vypíšu do terminálu.

## 3. Tri hlavné panely

| Panel | Čo ukazuje |
|---|---|
| **Funkcie** (vľavo) | Všetky funkcie programu. ★ = hlavná funkcia `main`. Šedé = importy z knižníc. |
| **Stred** | Karty *Listing* (inštrukcie), *Pseudokód* (čitateľný kód), *Graf funkcie*, *Graf volaní*, *Reťazce*, *Importy*… |
| **Detaily** (vpravo) | Čo je na vybranej adrese a **kto ju používa** (cross-referencie). Tlačidlá Premenovať / Komentár / Záložka. |

## 4. Nájdite heslo (5 minút)

1. V ľavom paneli kliknite na **★ main** a prepnite na kartu **Pseudokód** (alebo kláves `2`).
   Uvidíte, že `main` volá `check_user` a potom `check_password`.
2. Kliknite na **check_password**. V pseudokóde je volanie `strcmp(…)` – porovnanie dvoch reťazcov.
3. Prepnite na **Listing** (`1`). Uvidíte konštanty, ktoré program skladá do premennej – sú to bajty textu
   `reversa` a `2026` (v pseudokóde ako čísla, v karte *Reťazce* ako `2026`).
4. Heslo je `reversa2026`. Overenie: `samples/bin/login_x64.elf admin reversa2026`.
5. Premenujte `check_password` na `overenie_hesla` (kláves `N`). Meno sa okamžite zmení v zozname, v pseudokóde
   `main` aj v x-refs. `Ctrl+Z` zmenu vráti, `Ctrl+Y` ju zopakuje.
6. Uložte projekt (`Ctrl+S`). Cestu uvidíte v hláške; neskôr ho otvoríte príkazom `reversa gui cesta.rvproj`.

## 5. Vlastný súbor

Kliknite na **Otvoriť súbor** alebo spustite `reversa gui /cesta/k/programu`. Podporované sú ELF (x86-64, ARM64,
x86, ARM), PE 32/64-bit a surové dáta (`reversa gui firmware.bin --arch arm64 --base 0x8000000`).

## Ďalej

- [tutorials.md](tutorials.md) – „Nájdite hlavnú funkciu programu“, „Pochopte slučku“, ukážky s riešeniami.
- [scripting-tutorial.md](scripting-tutorial.md) – automatizácia v Pythone.
- [glossary.md](glossary.md) – slovník pojmov.
