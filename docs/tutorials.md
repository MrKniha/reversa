# Tutoriály

Všetky tutoriály pracujú s ukážkovými binárkami v `samples/bin/`. Sú to malé
programy, ktoré sme sami skompilovali zo zdrojov v `samples/src/` (kontrolné súčty
sú v `samples/bin/SHA256SUMS`). Nie je to malvér, môžete ich bezpečne otvárať.

Ak ste REVERSA ešte nespúšťali, začnite rýchlym štartom v [quickstart.md](quickstart.md).
Neznáme pojmy nájdete v [slovníku](glossary.md).

---

## Tutoriál 1: Prvá analýza za 10 minút

**Cieľ:** nájsť heslo a vlajku v programe `login`, bez toho, aby ste ho spustili.

1. **Spustite demo (1 min).**
   ```bash
   reversa --demo
   ```
   Otvorí sa prehliadač s rozhraním REVERSA a súborom `login_x64.elf`. Vľavo je
   zoznam funkcií, v strede listing (disassembly), vpravo pseudokód.

2. **Pozrite si reťazce (2 min).** Kliknite na kartu **Reťazce**. Uvidíte texty ako
   `Usage: login <user> <password>`, `Wrong password.` a `Access granted. Flag: ...`.
   Reťazce sú najrýchlejšia stopa: povedia vám, čo program robí.

3. **Kto reťazec používa? (2 min).** Kliknite na `Wrong password.`. V pravom paneli
   **Detaily** uvidíte sekciu *Kto to používa (x-refs)*: na reťazec sa odkazuje
   z `main`. Kliknutím na odkaz tam skočíte.

4. **Prečítajte pseudokód `main` (2 min).** V pravom paneli uvidíte približne toto:
   ```c
   if (check_user(*(int64_t *)(local_10 + 8)) != 0) {
       if (check_password(*(int64_t *)(local_10 + 16)) == 0) {
           puts("Wrong password.");
   ```
   `local_10` je `argv`, takže `local_10 + 8` je `argv[1]` (meno) a `local_10 + 16`
   je `argv[2]` (heslo). Premenujte `local_10` na `argv` (kláves **N**).

5. **Kto volá `strcmp`? (2 min).** Na karte **Importy** kliknite na `strcmp` a pozrite
   sa na panel **Detaily**. Volajú ho dve funkcie: `check_user` a `check_password`. Otvorte
   `check_password`: pseudokód ukáže, že heslo sa skladá z `"reversa"` a `"2026"`.

6. **Premenujte a uložte (1 min).** Kliknite na premennú alebo funkciu a stlačte
   **N**. Nové meno sa hneď prejaví v listingu, pseudokóde, grafe aj v zozname
   funkcií. Uložte projekt (**Ctrl+S**). Po opätovnom otvorení sú mená späť.

**Riešenie:** používateľ `admin` alebo `student`, heslo `reversa2026`, vlajka
`CTF{xrefs_are_your_friend}`.

---

## Tutoriál 2: Nájdite hlavnú funkciu programu

**Cieľ:** nájsť `main` v súbore bez symbolov (`login_x64_stripped.elf`).

Binárka bez symbolov (stripped) nemá mená funkcií. Ostanú len adresy, napríklad
`sub_401196`. Ako sa dostať k `main`?

1. Otvorte súbor:
   ```bash
   reversa gui samples/bin/login_x64_stripped.elf
   ```
2. Pozrite sa na **vstupný bod** (entry point, funkcia `_start`). Na Linuxe
   s glibc volá `_start` funkciu `__libc_start_main` a ako prvý argument jej odovzdá
   adresu `main`. V listingu `_start` uvidíte niečo ako:
   ```
   mov   rdi, 0x40125e        ; prvý argument = adresa main
   call  qword ptr [__libc_start_main]
   ```
3. REVERSA tento vzor rozpozná automaticky a funkciu na adrese `0x401298`
   pomenuje `main` (v zozname funkcií so ★). Overte si to: kliknite na adresu
   v `mov rdi, ...` a skočíte na začiatok `main`.
4. Ostatné funkcie zostali `sub_...`. Otvorte pseudokód `main`: volá
   `sub_401217(argv[1])` a `sub_401196(argv[2])`. Podľa reťazcov okolo
   (`Unknown user.`, `Wrong password.`) ich premenujte na `check_user` a
   `check_password` (kláves **N**).

**Kontrolná otázka:** prečo by ste nemali hľadať `main` len podľa toho, že je
„prvá veľká funkcia"? (Odpoveď: poradie funkcií v súbore určuje linker, nie
programátor. Spoľahlivejší je dátový tok z `_start`.)

**Pokročilé:** v pseudokóde `sub_401196` (stripped verzia) uvidíte čísla
`0x61737265766572` a `0x36323032`. Sú to reťazce zapísané ako čísla
(little-endian). Prečítajte bajty odzadu: `72 65 76 65 72 73 61` = `"reversa"`,
`32 30 32 36` = `"2026"`. Kompilátor takto optimalizoval `strcpy`/`strcat`.
Riadok `if (local_8 - __fs_28 == 0)` je kontrola **stack canary** (ochrana pred
pretečením zásobníka), nie logika programu.

---

## Tutoriál 3: Pochopte slučku

**Cieľ:** rozpoznať cyklus `for` v assembleri, v grafe aj v pseudokóde.

1. Otvorte `samples/bin/hello_x64.elf` a vyberte funkciu `sum_to`.
2. **Graf (CFG):** prepnite stred na **Graf funkcie** (kláves **3**). Uvidíte bloky
   a jednu hranu, ktorá ide **naspäť nahor**. Spätná hrana je znak slučky
   a REVERSA ju kreslí inou farbou. Blok, do ktorého vedie, je hlavička slučky.
3. **Listing:** v hlavičke slučky nájdete porovnanie a podmienený skok:
   ```
   cmp   eax, dword ptr [rbp - 0x14]
   jle   ...                         ; skoč späť do tela, ak i <= n
   ```
4. **Pseudokód:**
   ```c
   local_8 = 0;
   local_4 = 1;
   while (local_4 <= local_14) {
       local_8 += local_4;
       local_4++;
   }
   return local_8;
   ```
   Premenujte `local_4` na `i`, `local_8` na `total` a `local_14` na `n`.
   Teraz je zrejmé, že funkcia počíta súčet 1 + 2 + … + n.

**Prečo `while` a nie `for`?** V strojovom kóde nie je rozdiel medzi `for`
a `while`. Dekompilátor vždy vyberie jednu formu. `for (i = 1; i <= n; i++)`
je to isté ako `i = 1; while (i <= n) { …; i++; }`.

**Cvičenie:** nájdite slučku vo funkcii `greet`. Akú hodnotu má výraz
`greetings[i % 3]`, keď `i = 4`? (Riešenie: `"Hello"`; tabuľku `greetings`
nájdete cez x-refs na reťazec `Ahoj`.)

---

## Tutoriál 4: Rovnaký program, iná architektúra

Otvorte `samples/bin/login_arm64.elf` (AArch64) a `samples/bin/login_x86.exe`
(32-bit Windows PE). Pseudokód `main` vyzerá takmer rovnako ako pri x86-64, aj keď
listing je úplne iný. Všimnite si:

- ARM64 načítava adresy reťazcov dvojicou `adrp` + `add`. REVERSA ich spojí
  a v listingu aj v pseudokóde ukáže priamo reťazec.
- 32-bit Windows odovzdáva argumenty cez zásobník (`push`), nie cez registre.
- Windows PE volá funkcie z DLL cez tabuľku importov (IAT), napríklad
  `call dword ptr [__imp__strcmp]`.

---

## Tutoriál 5: Bez GUI (headless)

```bash
reversa analyze samples/bin/login_x64.elf --report report.md
reversa analyze samples/bin/login_x64.elf --pseudocode main
reversa search samples/bin/login_x64.elf --insn "call.*strcmp"
reversa script examples/scripts/strcmp_callers.py samples/bin/login_x64.elf
```

Report `report.md` obsahuje prehľad súboru, sekcie, importy, funkcie, reťazce,
graf volaní a pseudokód `main`. Ďalej pokračujte
[tutoriálom skriptovania](scripting-tutorial.md).
