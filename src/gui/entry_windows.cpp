// Entry point of reversa-gui.exe (Windows GUI subsystem: no console window on double click).
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cli/cli.hpp"
#include "core/text.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args{"gui"};
    for (int i = 1; i < argc; i++) args.push_back(rv::path_to_utf8(std::filesystem::path(argv[i])));
    LocalFree(argv);
    return rv::cli::main(args);
}
