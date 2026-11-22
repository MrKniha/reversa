# Zostavenie zo zdrojov

Hotové balíky pre Windows a Linux sú v [README](../README.md#inštalácia). Tento návod je pre vývojárov a pre tých, ktorí chcú REVERSA zostaviť sami.

## Zostavenie

Potrebujete C++20 kompilátor (GCC 11+, Clang 14+ alebo Visual Studio 2022),
CMake 3.21+, Qt 6.2+ a SQLite 3. Capstone stiahne CMake automaticky v pevnej verzii.

**Linux (Debian/Ubuntu):**

```bash
sudo apt install build-essential cmake ninja-build git qt6-base-dev libgl-dev libsqlite3-dev
git clone https://github.com/MrKniha/reversa.git && cd reversa
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/reversa --demo
```

Fedora: `sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel sqlite-devel`.

**Windows:** nainštalujte Visual Studio 2022 (Desktop development with C++),
Qt 6 (Qt Online Installer, komponent MSVC 2022 64-bit) a vcpkg (`git clone https://github.com/microsoft/vcpkg` a
`bootstrap-vcpkg.bat`). V „x64 Native Tools Command Prompt":

```bat
git clone https://github.com/MrKniha/reversa.git && cd reversa
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release
build\bin\reversa.exe --demo
```

Voľby CMake: `-DREVERSA_GUI=OFF` (len príkazový riadok, bez Qt), `-DREVERSA_TESTS=OFF`.

## Testy

```bash
ctest --test-dir build --output-on-failure           # C++ testy (API, pluginy, CLI, golden súbory) a GUI test
build/bin/headless_check build/bin/reversa .         # end-to-end kontrola všetkých vzoriek
build/bin/check_licenses .                           # licencie závislostí
build/bin/i18n_check .                               # úplnosť prekladov GUI
```

Na vývoj pozri [CONTRIBUTING.md](../CONTRIBUTING.md).

## Pluginy a skripty z buildu

Príklady v `examples/` sa preložia spolu s programom do `build/bin/examples`:

```bash
./build/bin/reversa script build/bin/examples/scripts/strcmp_callers.so samples/bin/login_x64.elf
./build/bin/reversa analyze samples/bin/login_x64.elf --plugins build/bin/examples/plugins
```

Návody: [scripting-tutorial.md](scripting-tutorial.md), [plugin-tutorial.md](plugin-tutorial.md).
