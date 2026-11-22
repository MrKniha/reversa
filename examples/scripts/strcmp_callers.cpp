// Prints every function that calls strcmp.
//   reversa script strcmp_callers.so samples/bin/login_x64.elf
#include <reversa/sdk.hpp>

#include <cstdio>

int run(const rv_host* host, const std::vector<std::string>& args) {
    auto prog = reversa::Program::open(host, args.size() > 1 ? args[1] : "samples/bin/login_x64.elf");
    for (auto& f : prog.callers_of("strcmp")) std::printf("%-24s %s\n", f.name.c_str(), reversa::hex(f.addr).c_str());
    return 0;
}
REVERSA_SCRIPT(run)
