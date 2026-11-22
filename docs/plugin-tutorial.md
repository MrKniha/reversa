# Tutoriál: napíšte si plugin

Plugin rozširuje REVERSA o vlastnú logiku: reaguje na udalosti (načítanie súboru,
nájdená funkcia, premenovanie) a môže pridať vlastný pohľad. Pluginy používajú
rovnaké Python API ako skripty (pozri [scripting-tutorial.md](scripting-tutorial.md)).

> **Bezpečnosť:** plugin je bežný Python kód. Beží v procese REVERSA **bez
> sandboxu** a má rovnaké práva ako vy. Inštalujte iba pluginy, ktorých kód ste
> si prečítali alebo ktorým dôverujete.

## 1. Štruktúra pluginu

Plugin je priečinok s dvomi súbormi:

```
hello_plugin/
├── plugin.json     # manifest
└── plugin.py       # kód
```

`plugin.json`:

```json
{
  "name": "hello-plugin",
  "version": "1.0.0",
  "api_version": "1.0",
  "entry": "plugin.py",
  "description": "Počíta funkcie a reaguje na premenovanie."
}
```

- `api_version` musí mať rovnaké hlavné číslo ako REVERSA (teraz `1`). Inak sa
  plugin nenačíta a v zozname pluginov má stav *incompatible*.
- `entry` musí byť súbor vnútri priečinka pluginu.

## 2. Kód pluginu

`plugin.py` definuje funkciu `register(api)`. Dostane objekt `Program` a vráti
objekt s háčikmi (hooks). Všetky háčiky sú nepovinné.

```python
class HelloPlugin:
    def __init__(self):
        self.functions = 0
        self.renames = []

    def on_load(self, program):
        print(f"[hello-plugin] načítaný súbor: {program.path}")

    def on_function_analyzed(self, program, func):
        self.functions += 1
        if func.name == "main":
            program.comment(func.addr, "hello-plugin: tu začína program")

    def on_rename(self, program, addr, old, new):
        self.renames.append((addr, old, new))
        print(f"[hello-plugin] {old} -> {new}")

    def on_unload(self, program):
        print(f"[hello-plugin] videl som {self.functions} funkcií")


def register(api):
    plugin = HelloPlugin()
    api.add_view("Hello plugin",
                 lambda p: f"Analyzovaných funkcií: {plugin.functions}\nPremenovaní: {len(plugin.renames)}")
    return plugin
```

| Háčik | Kedy sa volá |
|---|---|
| `on_load(program)` | po načítaní súboru, pred analýzou |
| `on_function_analyzed(program, func)` | raz pre každú nájdenú funkciu |
| `on_rename(program, addr, old, new)` | po každom premenovaní (aj pri undo/redo) |
| `on_unload(program)` | pri zatváraní súboru alebo ukončení |

Ak `register` nevráti nič, REVERSA hľadá háčiky ako funkcie priamo v module.

## 3. Inštalácia a spustenie

Skopírujte priečinok pluginu do `~/.reversa/plugins/` (Windows:
`%USERPROFILE%\.reversa\plugins\`). Prípadne použite premennú prostredia
`REVERSA_PLUGIN_PATH` alebo prepínač `--plugins`:

```bash
reversa analyze samples/bin/hello_x64.elf --plugins examples/plugins
reversa gui samples/bin/hello_x64.elf --plugins examples/plugins
```

Výstup v termináli:

```
[hello-plugin] načítaný súbor: .../samples/bin/hello_x64.elf
```

V GUI nájdete pluginy v menu **Pokročilé → Pluginy** (meno, verzia, stav, háčiky,
počet volaní). Pohľad „Hello plugin“ je na karte **Pohľady**. Ak pluginy nechcete,
použite `--no-plugins`.

## 4. Čo keď plugin spadne

V `examples/plugins/crashing_plugin` je plugin, ktorý zámerne vyhodí výnimku
v `on_function_analyzed`. REVERSA:

1. zachytí výnimku a plugin **vypne** (stav *failed*),
2. zapíše chybu do diagnostiky (s tracebackom pre autora pluginu),
3. pokračuje v analýze. Ostatné pluginy bežia ďalej.

```
! error: Plugin 'crashing-plugin' spadol v on_function_analyzed a bol vypnutý. REVERSA pokračuje ďalej.
```

Rovnako sa správa plugin, ktorý sa nedá ani načítať (syntaktická chyba, chýbajúci
súbor). Toto správanie overujú testy `tests/python/test_plugins.py` a
`tests/gui/test_gui.cpp` (háčiky bežia aj na pracovnom vlákne analýzy v GUI).

## 5. Väčší príklad: kryptografické konštanty

[examples/plugins/crypto_constants](../examples/plugins/crypto_constants) hľadá
v súbore známe konštanty (AES S-box, SHA-256, MD5, CRC32) cez
`program.search_bytes(...)`. Ku každému nálezu pridá záložku a vytvorí pohľad
„Krypto konštanty“. Podobne fungujú pluginy FindCrypt pre IDA a Ghidra.

## Tipy

- Háčik `on_function_analyzed` sa volá veľakrát. Robte v ňom len rýchle veci.
- Na anotácie použite API (`program.rename`, `program.comment`,
  `program.bookmark`), aby fungovalo undo a uloženie do projektu.
- Plugin by nemal sieťovo komunikovať ani spúšťať analyzovaný súbor.
  REVERSA je nástroj na statickú analýzu bez telemetrie.
