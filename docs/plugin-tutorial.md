# Tutoriál: napíšte si plugin v C++

Plugin rozširuje REVERSA o vlastnú logiku: reaguje na udalosti (načítanie súboru,
nájdená funkcia, premenovanie) a môže pridať vlastný pohľad. Plugin je zdieľaná
knižnica (`.so` na Linuxe, `.dll` na Windows) v C alebo C++. Používa rovnaké
rozhranie ako skripty (pozri [scripting-tutorial.md](scripting-tutorial.md)).

> **Bezpečnosť:** plugin je natívny kód. Beží v procese REVERSA **bez sandboxu**
> a má rovnaké práva ako vy. Inštalujte iba pluginy, ktorých kód ste si prečítali
> alebo ktorým dôverujete.

## 1. Kód pluginu

S pomocníkom [sdk.hpp](../sdk/include/reversa/sdk.hpp) je plugin trieda odvodená
od `reversa::Plugin`. Všetky háčiky sú nepovinné. Člen `program` je otvorený
program (rovnaké API ako v skriptoch).

```cpp
#include <reversa/sdk.hpp>

#include <string>
#include <vector>

struct HelloPlugin : reversa::Plugin {
    int functions = 0;
    std::vector<std::string> renames;

    void on_load() override {
        program.log("[hello-plugin] loaded file: " + program.path());
        program.add_view("Hello plugin", [this] {
            return "Functions analysed: " + std::to_string(functions) + "\nRenames: " + std::to_string(renames.size());
        });
    }
    void on_function_analyzed(const reversa::Function& f) override {
        functions++;
        if (f.name == "main") program.set_comment(f.addr, "hello-plugin: the program starts here");
    }
    void on_rename(reversa::Addr, const std::string& old_name, const std::string& new_name) override {
        renames.push_back(old_name + " -> " + new_name);
        program.log("[hello-plugin] " + old_name + " -> " + new_name);
    }
    void on_unload() override { program.log("[hello-plugin] saw " + std::to_string(functions) + " functions"); }
};

REVERSA_PLUGIN(HelloPlugin, "hello-plugin", "1.0.0", "Counts functions and reacts to renames.")
```

Celý súbor: [examples/plugins/hello_plugin.cpp](../examples/plugins/hello_plugin.cpp).

| Háčik | Kedy sa volá |
|---|---|
| `on_load()` | po načítaní súboru, pred analýzou |
| `on_function_analyzed(func)` | raz pre každú nájdenú funkciu |
| `on_rename(addr, old, new)` | po každom premenovaní (aj pri undo/redo) |
| `on_unload()` | pri zatváraní súboru alebo ukončení |

`REVERSA_PLUGIN` exportuje funkciu `reversa_plugin()`, ktorá vráti štruktúru
`rv_plugin` s menom, verziou, popisom a verziou API. Pre každý otvorený súbor
vznikne nový objekt pluginu.

Plugin v čistom C vyplní štruktúru `rv_plugin` sám, pozri
[sdk/include/reversa/plugin.h](../sdk/include/reversa/plugin.h) a testovací plugin
[tests/cpp/plugins/counter.c](../tests/cpp/plugins/counter.c).

## 2. Preklad

```bash
# Linux
g++ -std=c++17 -shared -fPIC -o hello_plugin.so hello_plugin.cpp
```

```bat
:: Windows (Developer Command Prompt pre Visual Studio)
cl /std:c++17 /EHsc /LD /I C:\reversa\bin\sdk\include hello_plugin.cpp
```

Plugin nepotrebuje linkovať REVERSA. Všetko volá cez tabuľku funkcií, ktorú dostane
pri načítaní.

## 3. Inštalácia a spustenie

Skopírujte knižnicu do `~/.reversa/plugins/` (Windows:
`%USERPROFILE%\.reversa\plugins\`). Môže byť aj v podpriečinku, napríklad
`plugins/hello/hello_plugin.so`. Ďalšie priečinky pridáte premennou prostredia
`REVERSA_PLUGIN_PATH` (oddeľovač `:` na Linuxe, `;` na Windows) alebo prepínačom
`--plugins`:

```bash
reversa analyze samples/bin/hello_x64.elf --plugins build/bin/examples/plugins
reversa gui samples/bin/hello_x64.elf --plugins build/bin/examples/plugins
```

Výstup v termináli:

```
[hello-plugin] loaded file: .../samples/bin/hello_x64.elf
```

V GUI nájdete pluginy v menu **Pokročilé → Pluginy** (meno, verzia, stav, háčiky,
počet volaní). Pohľad „Hello plugin“ je na karte **Pohľady**. Ak pluginy nechcete,
použite `--no-plugins`.

## 4. Čo keď plugin zlyhá

V [examples/plugins/crashing_plugin.cpp](../examples/plugins/crashing_plugin.cpp)
je plugin, ktorý zámerne vyhodí výnimku v `on_function_analyzed`. REVERSA:

1. dostane od háčika chybu (makro `REVERSA_PLUGIN` výnimku zachytí) a plugin
   **vypne** (stav *failed*),
2. zapíše chybu do diagnostiky aj s textom výnimky,
3. pokračuje v analýze. Ostatné pluginy bežia ďalej.

```
! error: Plugin 'crashing-plugin' failed in on_function_analyzed and was disabled. REVERSA continues.
```

Rovnako sa správa knižnica, ktorá sa nedá načítať, nemá funkciu
`reversa_plugin()` alebo je preložená pre inú verziu API (stav *incompatible*).
Toto správanie overujú testy [tests/cpp/test_plugins.cpp](../tests/cpp/test_plugins.cpp)
a `tests/gui/test_gui.cpp` (háčiky bežia aj na pracovnom vlákne analýzy v GUI).

> Natívny plugin môže zhodiť celú aplikáciu, napríklad zlým ukazovateľom. REVERSA
> zachytí chyby, ktoré plugin ohlási, ale nie pád pamäte. Aj preto inštalujte len
> pluginy, ktorým dôverujete.

## 5. Väčší príklad: kryptografické konštanty

[examples/plugins/crypto_constants.cpp](../examples/plugins/crypto_constants.cpp)
hľadá v súbore známe konštanty (AES S-box, SHA-256, MD5, CRC32) cez
`program.search_bytes(...)`. Ku každému nálezu pridá záložku a vytvorí pohľad
„Crypto constants“. Podobne fungujú pluginy FindCrypt pre IDA a Ghidra.

## Tipy

- Háčik `on_function_analyzed` sa volá veľakrát. Robte v ňom len rýchle veci.
- Háčiky môžu bežať na pracovnom vlákne analýzy. REVERSA volania na jeden program
  serializuje, takže zámky v plugine netreba.
- Na anotácie použite API (`rename`, `set_comment`, `set_bookmark`), aby fungovalo
  undo a uloženie do projektu.
- Plugin by nemal sieťovo komunikovať ani spúšťať analyzovaný súbor.
  REVERSA je nástroj na statickú analýzu bez telemetrie.
- Pluginy v Pythone (`plugin.json` + `plugin.py`) z verzie 0.1 už REVERSA
  nenačítava.
