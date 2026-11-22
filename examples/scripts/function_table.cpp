// Function table as CSV: name, address, size, number of callers, called functions.
//   reversa script function_table.so samples/bin/hello_x64.elf > functions.csv
#include <reversa/sdk.hpp>

#include <cstdio>
#include <string>

int run(const rv_host* host, const std::vector<std::string>& args) {
    auto prog = reversa::Program::open(host, args.size() > 1 ? args[1] : "samples/bin/hello_x64.elf");
    std::printf("name,address,size,callers,callees\n");
    for (auto& f : prog.functions()) {
        std::string callees;
        for (auto& c : prog.callees_of(f.addr)) callees += (callees.empty() ? "" : " ") + c.name;
        std::printf("%s,%s,%llu,%zu,%s\n", f.name.c_str(), reversa::hex(f.addr).c_str(),
                    static_cast<unsigned long long>(f.size), prog.callers_of(f.addr).size(), callees.c_str());
    }
    if (auto m = prog.main()) {  // reading memory: the first 16 bytes of main
        std::string bytes;
        char b[4];
        for (auto x : prog.read(m->addr, 16)) std::snprintf(b, sizeof b, "%02x ", x), bytes += b;
        std::fprintf(stderr, "# main bytes: %s\n", bytes.c_str());
    }
    return 0;
}
REVERSA_SCRIPT(run)
