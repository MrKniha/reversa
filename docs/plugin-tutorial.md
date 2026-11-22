# Tutorial: write a plugin in C++

A plugin extends REVERSA with your own logic: it reacts to events (file loaded, function found,
rename) and can add its own view. A plugin is a shared library (`.so` on Linux, `.dll` on Windows)
written in C or C++. It uses the same interface as scripts (see
[scripting-tutorial.md](scripting-tutorial.md)).

> **Security:** a plugin is native code. It runs inside the REVERSA process **without a sandbox**
> and has the same permissions as you. Only install plugins whose code you have read or that you
> trust.

## 1. Plugin code

With the helper [sdk.hpp](../sdk/include/reversa/sdk.hpp), a plugin is a class derived from
`reversa::Plugin`. All hooks are optional. The member `program` is the opened program (the same
API as in scripts).

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

The whole file: [examples/plugins/hello_plugin.cpp](../examples/plugins/hello_plugin.cpp).

| Hook | When it is called |
|---|---|
| `on_load()` | after the file is loaded, before the analysis |
| `on_function_analyzed(func)` | once for every function found |
| `on_rename(addr, old, new)` | after every rename (also on undo/redo) |
| `on_unload()` | when the file is closed or REVERSA quits |

`REVERSA_PLUGIN` exports the function `reversa_plugin()`, which returns an `rv_plugin` structure
with the name, version, description and API version. A new plugin object is created for every
opened file.

A plugin in plain C fills in the `rv_plugin` structure itself; see
[sdk/include/reversa/plugin.h](../sdk/include/reversa/plugin.h) and the test plugin
[tests/cpp/plugins/counter.c](../tests/cpp/plugins/counter.c).

## 2. Build

```bash
# Linux
g++ -std=c++17 -shared -fPIC -o hello_plugin.so hello_plugin.cpp
```

```bat
:: Windows (Developer Command Prompt for Visual Studio)
cl /std:c++17 /EHsc /LD /I C:\reversa\bin\sdk\include hello_plugin.cpp
```

A plugin does not link against REVERSA. It calls everything through the function table it gets
when it is loaded.

## 3. Install and run

Copy the library to `~/.reversa/plugins/` (Windows: `%USERPROFILE%\.reversa\plugins\`). It can
also be in a subfolder, for example `plugins/hello/hello_plugin.so`. Add more folders with the
environment variable `REVERSA_PLUGIN_PATH` (separator `:` on Linux, `;` on Windows) or with the
`--plugins` option:

```bash
reversa analyze samples/bin/hello_x64.elf --plugins build/bin/examples/plugins
reversa gui samples/bin/hello_x64.elf --plugins build/bin/examples/plugins
```

Output in the terminal:

```
[hello-plugin] loaded file: .../samples/bin/hello_x64.elf
```

In the GUI the plugins are listed in **Advanced → Plugins** (name, version, status, hooks, number
of calls). The "Hello plugin" view is on the **Views** tab. If you do not want plugins, use
`--no-plugins`.

## 4. When a plugin fails

[examples/plugins/crashing_plugin.cpp](../examples/plugins/crashing_plugin.cpp) throws an
exception in `on_function_analyzed` on purpose. REVERSA:

1. gets an error from the hook (the `REVERSA_PLUGIN` macro catches the exception) and **disables**
   the plugin (status *failed*),
2. writes the error, including the exception text, to the diagnostics,
3. continues the analysis. The other plugins keep running.

```
! error: Plugin 'crashing-plugin' failed in on_function_analyzed and was disabled. REVERSA continues.
```

A library that cannot be loaded, has no `reversa_plugin()` function or is built for another API
version (status *incompatible*) is handled the same way. The tests
[tests/cpp/test_plugins.cpp](../tests/cpp/test_plugins.cpp) and `tests/gui/test_gui.cpp` check
this behaviour (hooks also run on the analysis worker thread in the GUI).

> A native plugin can bring down the whole application, for example with a bad pointer. REVERSA
> catches errors that a plugin reports, but not memory crashes. That is another reason to install
> only plugins you trust.

## 5. A bigger example: crypto constants

[examples/plugins/crypto_constants.cpp](../examples/plugins/crypto_constants.cpp) looks for
well-known constants (AES S-box, SHA-256, MD5, CRC32) with `program.search_bytes(...)`. It adds a
bookmark for every hit and creates the view "Crypto constants". FindCrypt plugins for IDA and
Ghidra work in a similar way.

## Tips

- The `on_function_analyzed` hook is called many times. Only do quick work in it.
- Hooks may run on the analysis worker thread. REVERSA serialises calls to one program, so the
  plugin needs no locks.
- Use the API for annotations (`rename`, `set_comment`, `set_bookmark`), so undo and saving to the
  project work.
- A plugin should not use the network or run the analysed file. REVERSA is a static analysis tool
  without telemetry.
- REVERSA no longer loads Python plugins (`plugin.json` + `plugin.py`) from version 0.1.
