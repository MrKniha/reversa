# Building from source

Packages for Windows and Linux are listed in the [README](../README.md#installation). This guide
is for developers and anyone who wants to build REVERSA themselves.

## Build

You need a C++20 compiler (GCC 11+, Clang 14+ or Visual Studio 2022), CMake 3.21+, Qt 6.2+ and
SQLite 3. CMake downloads Capstone automatically at a pinned version.

Linux (Debian/Ubuntu):

```bash
sudo apt install build-essential cmake ninja-build git qt6-base-dev libgl-dev libsqlite3-dev
git clone https://github.com/MrKniha/reversa.git && cd reversa
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/reversa --demo
```

Fedora: `sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel sqlite-devel`.

Windows: install Visual Studio 2022 (Desktop development with C++), Qt 6 (Qt Online
Installer, component MSVC 2022 64-bit) and vcpkg (`git clone https://github.com/microsoft/vcpkg`
and `bootstrap-vcpkg.bat`). In the "x64 Native Tools Command Prompt":

```bat
git clone https://github.com/MrKniha/reversa.git && cd reversa
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release
build\bin\reversa.exe --demo
```

CMake options: `-DREVERSA_GUI=OFF` (command line only, no Qt), `-DREVERSA_TESTS=OFF`.

## Tests

```bash
ctest --test-dir build --output-on-failure           # C++ tests (API, plugins, CLI, golden files) and the GUI test
build/bin/headless_check build/bin/reversa .         # end-to-end check of all samples
build/bin/check_licenses .                           # dependency licenses
build/bin/i18n_check .                               # GUI translations are complete
```

For development, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Plugins and scripts from the build

The examples in `examples/` are built together with the program into `build/bin/examples`:

```bash
./build/bin/reversa script build/bin/examples/scripts/strcmp_callers.so samples/bin/login_x64.elf
./build/bin/reversa analyze samples/bin/login_x64.elf --plugins build/bin/examples/plugins
```

Guides: [scripting-tutorial.md](scripting-tutorial.md), [plugin-tutorial.md](plugin-tutorial.md).
