// Program entry point of the console executable `reversa` (CLI + GUI).
#include <filesystem>
#include <string>
#include <vector>

#include "cli/cli.hpp"
#include "core/text.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);  // UTF-8 output (names, strings) on Windows consoles
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(rv::path_to_utf8(std::filesystem::path(argv[i])));
    return rv::cli::main(args);
}
#else
int main(int argc, char** argv) {
    return rv::cli::main(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
