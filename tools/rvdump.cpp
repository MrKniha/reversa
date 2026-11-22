// Developer tool: dumps analysis facts as JSON (compared against the reference implementation in CI).
#include <cstdio>
#include <cctype>
#include <iostream>
#include <map>

#include "analysis/analysis.hpp"
#include "core/text.hpp"
#include "session/session.hpp"
#include "formats/formats.hpp"

using namespace rv;

// Pseudocode of every normal function, named by the session (no user annotations).
static void dump_pseudo(Session& s) {
    auto& a = s.require_analysis();
    for (auto& [ad, f] : a.functions) {
        if (f.kind != "normal") continue;
        std::cout << "== " << s.name_of(ad) << " " << hex_digits(ad) << "\n" << s.pseudocode(ad) << "\n";
    }
}

int main(int argc, char** argv) {
    bool pseudo = argc > 2 && std::string(argv[1]) == "--pseudo";
    if (argc < 2) { std::fprintf(stderr, "usage: rvdump [--pseudo] FILE\n"); return 2; }
    if (pseudo) {
        auto s = Session::open(argv[2]);
        dump_pseudo(*s);
        return 0;
    }
    Binary b = load_file(argv[1]);
    auto a = analyze(b);
    std::string o = "{";
    o += "\"format\":\"" + b.format + "\",\"arch\":\"" + b.arch + "\",\"entry\":" + (b.entry ? std::to_string(*b.entry) : "null");
    o += ",\"sections\":[";
    for (size_t i = 0; i < b.sections.size(); i++) {
        auto& s = b.sections[i];
        o += (i ? "," : "") + std::string("[\"") + json_escape(s.name) + "\"," + std::to_string(s.addr) + "," + std::to_string(s.size) + "," +
             std::to_string(s.offset) + "," + std::to_string(s.file_size) + ",\"" + s.perms() + "\"]";
    }
    o += "],\"symbols\":" + std::to_string(b.symbols.size()) + ",\"imports\":[";
    for (size_t i = 0; i < b.imports.size(); i++)
        o += (i ? "," : "") + std::string("[\"") + json_escape(b.imports[i].name) + "\"," + std::to_string(b.imports[i].slot) + "]";
    o += "],\"relocs\":" + std::to_string(b.relocations.size()) + ",\"compiler\":\"" + json_escape(b.compiler) + "\"";
    o += ",\"main\":" + (a->main ? std::to_string(*a->main) : "null") + ",\"functions\":[";
    bool first = true;
    for (auto& [ad, f] : a->functions) {
        o += (first ? "" : ",") + std::string("[") + std::to_string(ad) + ",\"" + f.kind + "\"," + std::to_string(f.size) + "," +
             std::to_string(f.insn_count) + "," + std::to_string(f.blocks.size()) + "]";
        first = false;
    }
    o += "],\"xrefs\":" + std::to_string(a->xrefs.size()) + ",\"strings\":" + std::to_string(a->strings.size());
    o += ",\"callers_strcmp\":[";
    auto it = a->import_addr.find("strcmp");
    if (it == a->import_addr.end()) it = a->import_addr.find("_strcmp");
    if (it != a->import_addr.end()) {
        auto c = a->callers(it->second);
        for (size_t i = 0; i < c.size(); i++) o += (i ? "," : "") + std::to_string(c[i]);
    }
    o += "]}";
    std::cout << o << "\n";
}
