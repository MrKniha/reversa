# Bezpečnosť a etika

Ako bezpečne analyzovať podozrivé súbory a na čo REVERSA slúži (a na čo nie). Hlásenie zraniteľností: [SECURITY.md](../SECURITY.md).

## Bezpečná analýza malvéru

REVERSA robí iba statickú analýzu. Súbor číta, ale nespúšťa. Napriek tomu
pri práci s podozrivými súbormi dodržujte tieto pravidlá:

1. **Pracujte vo virtuálnom stroji** (VirtualBox, VMware, QEMU/KVM), nie na
   svojom bežnom počítači. Pred analýzou si spravte snapshot.
2. **Odpojte sieť** virtuálneho stroja alebo použite izolovanú sieť
   (host-only). Malvér môže kontaktovať riadiace servery.
3. **Nikdy súbor nespúšťajte** mimo izolovaného prostredia. Ani „len na skúšku".
4. **Premenujte súbory** tak, aby sa nedali omylom spustiť (napr.
   `vzorka.exe_` alebo `vzorka.bin`). Archívy so vzorkami chráňte heslom
   (konvencia `infected`).
5. **Nezdieľajte priečinky** medzi hostiteľom a VM, kým to nepotrebujete.
   Vypnite automatické otváranie súborov a náhľady.
6. **Pozor na skripty a pluginy.** Bežia bez sandboxu. Nepoužívajte pluginy
   z neznámych zdrojov.
7. **Overte si hash** (SHA-256) vzorky pred a po analýze. REVERSA ho ukazuje
   v reporte, v tooltipe stavového riadku GUI a ukladá ho do projektu.
8. **Poškodené súbory sú v poriadku.** REVERSA načíta, čo sa dá, a ohlási
   chyby v diagnostike (*Pokročilé → Diagnostika načítania*). Nespadne.
9. **Dodržujte zákony a pravidlá organizácie.** Vzorky malvéru neposielajte
   ďalej mimo dôveryhodných kanálov (napr. CSIRT, výrobca antivírusu).

Do tohto repozitára nikdy nepridávajte malvér ani binárky bez zdrojového kódu.
Ukážky v `samples/` sú vlastné malé programy so zdrojmi v `samples/src/`.

## Etika

REVERSA slúži na učenie, CTF súťaže, analýzu malvéru, bezpečnostný výskum,
interoperabilitu a ladenie vlastného softvéru.

**REVERSA neobsahuje a nikdy nebude obsahovať funkcie na obchádzanie DRM,
licenčných kontrol, aktivácie, anti-cheat systémov alebo ochrany proti
kopírovaniu.** Takéto príspevky neprijmeme.

**Hranica patch módu:** budúci patch mód (roadmapa, nie je v 0.2.0) bude určený
na úpravy **vlastných** programov alebo programov, na ktoré máte povolenie:
opravy chýb, výučba, CTF úlohy. Nebude obsahovať automatické „crackovanie",
hľadanie a odstraňovanie licenčných kontrol ani podobné funkcie. Za to, ako
nástroj použijete, zodpovedáte vy. Analyzujte iba softvér, na ktorý máte právo.
