# Slovník pojmov (glossary)

Tieto pojmy používame v celom UI, dokumentácii aj chybových hláškach rovnako. Ak pridávate text do UI,
použite termín z tohto slovníka (a ak chýba, doplňte ho sem).

| Pojem v UI | Anglicky | Vysvetlenie |
|---|---|---|
| **Binárka / binárny súbor** | binary | Skompilovaný program alebo knižnica (napr. `.exe`, ELF), ktorú analyzujeme. |
| **Formát** | file format | Spôsob, akým je binárka uložená: ELF (Linux), PE (Windows), Mach-O (macOS, zatiaľ nepodporovaný), surové dáta. |
| **Surové dáta** | raw binary | Súbor bez hlavičky (dump firmvéru, shellcode). Architektúru a základnú adresu treba zadať alebo ich REVERSA odhadne. |
| **Architektúra** | architecture | Typ procesora: x86 (32-bit), x86-64, ARM (32-bit), ARM64 (AArch64). |
| **Endianita** | endianness | Poradie bajtov v čísle: little endian (najnižší bajt prvý) alebo big endian. |
| **Sekcia** | section | Pomenovaná časť súboru, napr. `.text` (kód), `.data` (dáta), `.rodata` (konštanty), `.bss` (nulové dáta). |
| **Segment** | segment | Časť súboru, ktorú zavádzač (loader) mapuje do pamäte; sekcie ležia v segmentoch. |
| **Práva r/w/x** | permissions | r = čítanie, w = zápis, x = spúšťanie kódu. |
| **Adresa** | address | Miesto v pamäti programu po zavedení (virtuálna adresa), zobrazujeme hexadecimálne, napr. `0x401000`. |
| **Offset v súbore** | file offset | Pozícia bajtu v súbore na disku (líši sa od adresy). |
| **RVA** | Relative Virtual Address | Adresa relatívna k základnej adrese obrazu (PE). |
| **Základná adresa** | image base | Adresa, na ktorú sa program zavádza (napr. `0x400000`). |
| **Vstupný bod (entry)** | entry point | Prvá inštrukcia, ktorú systém spustí. Väčšinou to nie je `main`, ale štartovací kód knižnice. |
| **Hlavná funkcia (main)** | main | Funkcia, kde začína „vlastný“ program. V zozname funkcií je označená ★. |
| **Funkcia** | function | Ucelený kus kódu s jedným začiatkom, ktorý sa dá zavolať. |
| **Import** | import | Funkcia z inej knižnice (napr. `printf` z libc), ktorú program volá. V zozname je šedá. |
| **Export** | export | Funkcia alebo dáta, ktoré binárka ponúka iným programom (typické pre knižnice/DLL). |
| **Thunk** | thunk | Malá „prestupná“ funkcia, ktorá len skočí na import (napr. `strcmp: jmp [__imp_strcmp]`). |
| **PLT / GOT** | Procedure Linkage Table / Global Offset Table | Tabuľky v ELF, cez ktoré program volá importy. |
| **IAT** | Import Address Table | Tabuľka adries importov v PE. |
| **Inštrukcia** | instruction | Jeden príkaz procesora, napr. `mov`, `call`, `jmp`. |
| **Listing** | listing / disassembly | Výpis inštrukcií za sebou s adresami a bajtami. |
| **Disassembler** | disassembler | Nástroj, ktorý prevádza bajty strojového kódu na čitateľné inštrukcie. |
| **Pseudokód** | pseudocode / decompiled code | Odhad pôvodného kódu v štýle C. V REVERSA MVP je **dočasný** – typy sú len odhadnuté. |
| **Dekompilátor** | decompiler | Nástroj, ktorý z inštrukcií vytvára pseudokód. |
| **Premenná a1, a2…** | parameter | 1., 2. … parameter funkcie. |
| **local_X** | local variable | Lokálna premenná uložená na zásobníku (X = posun v hexadecimálnom tvare). |
| **vN** | temporary | Pomocná premenná, ktorá vznikla z registra procesora. |
| **Blok** | basic block | Postupnosť inštrukcií bez skokov dovnútra alebo von okrem začiatku a konca. |
| **CFG / Graf funkcie** | control flow graph | Graf blokov jednej funkcie a skokov medzi nimi. |
| **Slučka** | loop | Časť kódu, ktorá sa opakuje (v grafe oranžový rámik a prerušovaná hrana späť). |
| **Graf volaní** | call graph | Graf, ktorý ukazuje, ktorá funkcia volá ktorú. |
| **Cross-referencia (x-ref)** | cross-reference | Odkaz z jedného miesta programu na iné: „volané z“, „čítané z“, „zapisované z“, „adresa použitá v“, „ukazovateľ v dátach“. |
| **Reťazec** | string | Text uložený v binárke (ASCII alebo UTF-16). |
| **Komentár** | comment | Vaša poznámka k adrese. Ukladá sa do projektu. |
| **Záložka** | bookmark | Pomenované miesto, ku ktorému sa chcete vrátiť. |
| **Projekt** | project (`.rvproj`) | Súbor s kópiou binárky a všetkými vašimi anotáciami (mená, komentáre, záložky). |
| **Anotácia** | annotation | Spoločný názov pre premenovania, komentáre a záložky. |
| **Analýza** | auto-analysis | Automatické hľadanie funkcií, reťazcov a x-refs po otvorení súboru (beží na pozadí). |
| **Diagnostika** | diagnostics | Upozornenia z načítania (napr. poškodená časť súboru). |
| **Plugin** | plugin | Knižnica v C/C++ (.so/.dll), ktorá reaguje na udalosti (načítanie, analýza funkcie, premenovanie). |
| **Skript** | script | Knižnica v C/C++, ktorú spustí `reversa script` a ktorá používa API REVERSA. |
| **Pohľad** | view | Vlastný textový výstup zo skriptu alebo pluginu, zobrazený v GUI na karte Pohľady. |
| **Report** | report | Textové zhrnutie analýzy (Markdown alebo HTML). |
| **Headless** | headless | Beh bez grafického rozhrania (CLI, CI). |
| **Hex výpis** | hex dump | Bajty zobrazené hexadecimálne spolu s ASCII znakmi. |
