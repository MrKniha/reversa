// Developer tool: dumps analysis facts as JSON (compared against the reference implementation in CI).
#include <cstdio>
#include <cctype>
#include <iostream>
#include <map>

#include "analysis/analysis.hpp"
#include "core/text.hpp"
#include "decompiler/decompiler.hpp"
#include "formats/formats.hpp"

using namespace rv;

// Pseudocode of every normal function, named like the session does (no user annotations).
static void dump_pseudo(const Binary& b, const Analysis& a) {
    std::map<Addr, std::string> syms;
    for (auto& s : b.symbols) syms.emplace(s.addr, s.name);
    for (auto& [ad, n] : a.auto_names)
        if (!syms.count(ad) || n == "main") syms[ad] = n;
    auto name_of = [&](Addr ad) -> std::string {
        auto it = syms.find(ad);
        if (it != syms.end() && !it->second.empty()) return it->second;
        if (a.functions.count(ad)) return "sub_" + hex_digits(ad);
        if (auto* st = a.string(ad)) {
            std::string slug, cur = utf8_prefix(st->text, 24);
            for (char c : cur) {
                bool ok = std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 128;
                if (ok) slug += c;
                else if (slug.empty() || slug.back() != '_') slug += '_';
            }
            while (!slug.empty() && slug.front() == '_') slug.erase(0, 1);
            while (!slug.empty() && slug.back() == '_') slug.pop_back();
            return slug.empty() ? "str_" + hex_digits(ad) : "s_" + slug + "_" + hex_digits(ad);
        }
        auto* fn = a.function_containing(ad);
        if (fn && fn->kind == "normal" && ad != fn->addr) return "loc_" + hex_digits(ad);
        if (b.is_code(ad)) return "code_" + hex_digits(ad);
        if (b.is_mapped(ad)) return "data_" + hex_digits(ad);
        return "0x" + hex_digits(ad);
    };
    RenderNames names;
    names.name_of = name_of;
    names.string_at = [&](Addr ad) -> std::optional<std::string> {
        if (auto* st = a.string(ad)) return st->text;
        return std::nullopt;
    };
    names.is_symbol = [&](int64_t v) {
        if (v < 0x100) return false;
        Addr ad = Addr(v);
        if (a.string_at.count(ad) || a.functions.count(ad)) return true;
        if (v >= 0x1000) {
            auto* s = b.section_at(ad);
            return s && !s->executable;
        }
        return false;
    };
    for (auto& [ad, f] : a.functions) {
        if (f.kind != "normal") continue;
        std::cout << "== " << name_of(ad) << " " << hex_digits(ad) << "\n";
        std::cout << to_text(render(decompile(a, ad), name_of(ad), names)) << "\n";
    }
}

int main(int argc, char** argv) {
    bool pseudo = argc > 2 && std::string(argv[1]) == "--pseudo";
    if (argc < 2) { std::fprintf(stderr, "usage: rvdump [--pseudo] FILE\n"); return 2; }
    Binary b = load_file(argv[pseudo ? 2 : 1]);
    auto a = analyze(b);
    if (pseudo) { dump_pseudo(b, *a); return 0; }
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
