# Tutorial: scripts in C++

A script is a small shared library (`.so` on Linux, `.dll` on Windows) that the `reversa script`
command runs. Everything you see in the GUI (functions, x-refs, pseudocode, renames) is also
available from a script. Scripts and plugins use the same stable C interface, version
`RV_API_VERSION 1` ([sdk/include/reversa/plugin.h](../sdk/include/reversa/plugin.h)). For C++
there is a thin helper on top of it with no other dependencies
([sdk/include/reversa/sdk.hpp](../sdk/include/reversa/sdk.hpp)).

## First script: who calls strcmp

```cpp
#include <reversa/sdk.hpp>

#include <cstdio>

int run(const rv_host* host, const std::vector<std::string>& args) {
    auto prog = reversa::Program::open(host, args.size() > 1 ? args[1] : "samples/bin/login_x64.elf");
    for (auto& f : prog.callers_of("strcmp")) std::printf("%-24s %s\n", f.name.c_str(), reversa::hex(f.addr).c_str());
    return 0;
}
REVERSA_SCRIPT(run)
```

The file is [examples/scripts/strcmp_callers.cpp](../examples/scripts/strcmp_callers.cpp).
`args[0]` is the path of the script, the other elements are the arguments after the script. The
return value of `run` is the exit code of `reversa script`. An exception (for example an unknown
function name) is printed as an error and the script ends with exit code 1.

## Build and run

The SDK headers ship with REVERSA: Linux `/usr/include/reversa`, Windows
`bin\sdk\include\reversa` in the unpacked zip. You do not link any library.

```bash
# Linux (g++ or clang++)
g++ -std=c++17 -shared -fPIC -o strcmp_callers.so strcmp_callers.cpp
reversa script ./strcmp_callers.so samples/bin/login_x64.elf
```

```bat
:: Windows (Developer Command Prompt for Visual Studio)
cl /std:c++17 /EHsc /LD /I C:\reversa\bin\sdk\include strcmp_callers.cpp
reversa script strcmp_callers.dll samples\bin\login_x64.exe
```

Output for `samples/bin/login_x64.elf`:

```
check_password           0x401196
check_user               0x401217
```

The script also works on Windows PE (`login_x64.exe`, import `strcmp` from msvcrt.dll), on ARM64
and on binaries without symbols. The packages contain the built examples: Linux
`/usr/lib/reversa/examples/scripts/`, Windows `bin\examples\scripts\`. When you develop REVERSA,
CMake builds them into `build/bin/examples/scripts/`.

## API overview (C++)

`reversa::Program::open(host, path)` opens a binary or a `.rvproj` project and analyses it. The
program is closed when the object goes away. Wherever the API expects a name you can also pass
`"0x401196"`, `"main"`, `"strcmp"` or `"sub_401196"`.

| Call | Returns / does |
|---|---|
| `path()`, `format()`, `arch()`, `entry()` | basic information |
| `main()` | the `main` function (`std::optional<Function>`) |
| `functions(include_imports = false)` | list of functions |
| `function(ref)` | one function (`addr`, `name`, `size`, `insn_count`, `block_count`, `is_import`) |
| `resolve(ref)`, `addr(ref)`, `name_of(addr)` | names and addresses (`addr` throws `reversa::Error` for an unknown name) |
| `callers_of(ref)`, `callees_of(addr)` | who calls the function and whom it calls (also through PLT/IAT) |
| `xrefs_to(addr)` | cross-references (`from`, `to`, `kind`) |
| `strings()` | strings found (`addr`, `text`) |
| `search_bytes("48 8b ?? 05")` | addresses where the pattern occurs (`??` = any byte) |
| `read(addr, size)` | bytes from the file |
| `pseudocode(addr)` | pseudocode as text |
| `rename(addr, name)`, `set_comment(addr, text)`, `set_bookmark(addr, label)` | annotations (with undo); empty text removes them |
| `comment(addr)`, `bookmark(addr)` | read annotations |
| `add_view(name, function)` | custom text view (GUI *Views* tab, reports) |
| `save(path.rvproj)` | save the project (without a path: next to the binary) |
| `log(text)` | print to standard output |

## Example: function table as CSV

[examples/scripts/function_table.cpp](../examples/scripts/function_table.cpp) prints the name,
address, size, number of callers and called functions:

```bash
reversa script function_table.so samples/bin/hello_x64.elf > functions.csv
```

## Example: automatic annotations and a saved project

[examples/scripts/annotate_and_view.cpp](../examples/scripts/annotate_and_view.cpp) comments the
places that use strings containing "password", renames the callers of `strcmp` and saves the
project. Open the result in the GUI:

```bash
reversa script annotate_and_view.so samples/bin/login_x64_stripped.elf out.rvproj
reversa gui out.rvproj
```

## A script in plain C

`sdk.hpp` is only a convenience. A script can use the `rv_host` function table directly:

```c
#include <stdio.h>
#include <reversa/plugin.h>

RV_EXPORT int reversa_script(const rv_host* host, int argc, const char* const* argv) {
    rv_program* p = host->open(argc > 1 ? argv[1] : "samples/bin/login_x64.elf");
    if (!p) { fprintf(stderr, "%s\n", host->last_error()); return 1; }
    printf("%s / %s, %zu functions\n", host->format(p), host->arch(p), host->function_count(p));
    host->close(p);
    return 0;
}
```

## Tips

- A rename from a script shows up at once in the pseudocode and in the x-refs. After you save the
  project, you see it in the GUI.
- Text returned by an `rv_host` function is valid only until the next call on the same thread.
  `sdk.hpp` copies it into a `std::string` for you.
- The pseudocode is **provisional** (MVP): types are guessed. Check important conclusions in the
  listing.
- A script runs inside the REVERSA process without a sandbox. Only run scripts you trust.
- REVERSA no longer runs Python scripts (`.py`) from version 0.1. Rewrite them following this
  guide.
