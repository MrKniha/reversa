// Finds well-known crypto constants in the binary (idea similar to the FindCrypt plugins for
// IDA/Ghidra). Adds a bookmark for every hit and a "Crypto constants" view.
#include <reversa/sdk.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {
const std::pair<const char*, const char*> SIGNATURES[] = {
    {"AES S-box", "63 7c 77 7b f2 6b 6f c5 30 01 67 2b fe d7 ab 76"},
    {"SHA-256 K[0..3]", "98 2f 8a 42 91 44 37 71 cf fb c0 b5 a5 db b5 e9"},
    {"SHA-256 H0 (LE)", "67 e6 09 6a 85 ae 67 bb"},
    {"MD5 T[0..1] (LE)", "78 a4 6a d7 56 b7 c7 e8"},
    {"CRC32 table", "00 00 00 00 96 30 07 77 2c 61 0e ee"},
};
}  // namespace

struct CryptoConstants : reversa::Plugin {
    std::vector<std::pair<std::string, reversa::Addr>> hits;

    void on_load() override {
        hits.clear();
        for (auto& [name, pattern] : SIGNATURES)
            for (auto addr : program.search_bytes(pattern)) {
                hits.emplace_back(name, addr);
                if (!program.bookmark(addr)) program.set_bookmark(addr, std::string("crypto: ") + name);
            }
        program.add_view("Crypto constants", [this] {
            if (hits.empty()) return std::string("No well-known crypto constants found.");
            std::string out;
            for (auto& [name, addr] : hits) out += reversa::hex(addr) + "  " + name + "\n";
            return out;
        });
    }
};

REVERSA_PLUGIN(CryptoConstants, "crypto-constants", "1.0.0", "Bookmarks well-known crypto constants (AES, SHA-256, MD5, CRC32).")
