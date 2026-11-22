# Quick start za 10 minút

Cieľ: nainštalovať REVERSA, otvoriť ukážkovú binárku a nájsť v nej heslo – bez predchádzajúcich znalostí.

## 1. Inštalácia (2 minúty)

Stiahnite hotový balík z GitHub Actions (posledný úspešný beh → *Artifacts*):

- **Windows:** `reversa-windows-x64` (zip). Rozbaľte ho a spustite `reversa-windows-x64\bin\reversa-gui.exe`.
  Pre príkazy nižšie otvorte v priečinku `bin` príkazový riadok a píšte `reversa.exe`.
- **Linux (Ubuntu 24.04 a novšie):** `reversa-linux-x64-deb`, potom `sudo apt install ./reversa_0.2.0_amd64.deb`.

Overenie: `reversa --version`. Zostavenie zo zdrojov popisuje [building.md](building.md).

## 2. Ukážková analýza (1 minúta)

```bash
reversa --demo
```

Otvorí sa okno REVERSA a na pozadí sa analyzuje ukážkový program (priebeh vidíte v stavovom riadku,
tlačidlom *Zrušiť* analýzu zastavíte). Pri prvom spustení uvidíte krátkeho sprievodcu – môžete ho preskočiť. Bez grafického prostredia (server, CI) použite
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

## Ovládanie GUI (klávesové skratky)

| Kláves | Akcia |
|---|---|
| `Ctrl+O` / `Ctrl+S` | otvoriť súbor alebo projekt / uložiť projekt |
| `Ctrl+F` | hľadať funkciu, reťazec alebo adresu |
| `G` | skočiť na adresu alebo meno |
| `1` `2` `3` `4` | listing, pseudokód, graf funkcie, graf volaní |
| `N` | premenovať funkciu, adresu alebo vybranú premennú v pseudokóde |
| `;` / `B` | komentár / záložka |
| `Ctrl+Z` / `Ctrl+Y` | späť / znova |
| `Alt+←` | späť v histórii navigácie |
| `Ctrl+Shift+T` | svetlý / tmavý motív |
| `F1` | úvodný sprievodca |

Všetky skratky zmeníte v *Zobraziť → Nastavenia*. Tam nastavíte aj veľkosť
písma, jazyk a vypnete vysvetlivky pre začiatočníkov. Predvolený jazyk je
angličtina; po zmene jazyka reštartujte REVERSA. Analýza beží na pozadí
a tlačidlom *Zrušiť* v stavovom riadku ju zastavíte.

## Ďalej

- [tutorials.md](tutorials.md) – „Nájdite hlavnú funkciu programu“, „Pochopte slučku“, ukážky s riešeniami.
- [scripting-tutorial.md](scripting-tutorial.md) – automatizácia skriptmi v C++.
- [glossary.md](glossary.md) – slovník pojmov.
