// ELF loader (32/64-bit, little/big endian; x86, x86-64, ARM, AArch64), written from the
// System V ABI / ELF v1.2 specification so every sub-structure can fail independently.
#include <algorithm>
#include <map>
#include <tuple>

#include "core/reader.hpp"
#include "formats/formats.hpp"

namespace rv {
namespace {

const std::map<uint16_t, std::pair<std::string, int>> MACHINES = {
    {3, {"x86", 32}}, {62, {"x86-64", 64}}, {40, {"arm", 32}}, {183, {"arm64", 64}}};
enum { SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4, SHT_DYNAMIC = 6, SHT_NOBITS = 8, SHT_REL = 9, SHT_DYNSYM = 11 };
enum { SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXEC = 4 };
enum { PT_LOAD = 1 };
enum { DT_NEEDED = 1 };
const std::map<std::string, std::map<uint32_t, std::string>> RELOC_NAMES = {
    {"x86-64", {{1, "R_X86_64_64"}, {6, "R_X86_64_GLOB_DAT"}, {7, "R_X86_64_JUMP_SLOT"}, {8, "R_X86_64_RELATIVE"}}},
    {"arm64", {{257, "R_AARCH64_ABS64"}, {1025, "R_AARCH64_GLOB_DAT"}, {1026, "R_AARCH64_JUMP_SLOT"}, {1027, "R_AARCH64_RELATIVE"}}},
    {"x86", {{1, "R_386_32"}, {6, "R_386_GLOB_DAT"}, {7, "R_386_JMP_SLOT"}, {8, "R_386_RELATIVE"}}},
    {"arm", {{2, "R_ARM_ABS32"}, {21, "R_ARM_GLOB_DAT"}, {22, "R_ARM_JUMP_SLOT"}, {23, "R_ARM_RELATIVE"}}},
};

struct Shdr { uint32_t name, type; uint64_t flags, addr, offset, size; uint32_t link, info; };
struct Sym { std::string name; uint64_t value, size; uint8_t info; uint16_t shndx; };

std::vector<Sym> read_symbols(const Reader& r, bool is64, const Shdr& sh, const std::vector<Shdr>& shdrs) {
    std::optional<uint64_t> strtab;
    if (sh.link < shdrs.size()) strtab = shdrs[sh.link].offset;
    uint64_t ent = is64 ? 24 : 16;
    std::vector<Sym> out;
    uint64_t n = std::min<uint64_t>(sh.size / ent, 500000);
    for (uint64_t k = 0; k < n; k++) {
        uint64_t o = sh.offset + k * ent;
        Sym s{};
        uint32_t st_name;
        if (is64) {
            r.need(o, 24);
            st_name = r.u32(o); s.info = r.u8(o + 4); s.shndx = r.u16(o + 6); s.value = r.u64(o + 8); s.size = r.u64(o + 16);
        } else {
            r.need(o, 16);
            st_name = r.u32(o); s.value = r.u32(o + 4); s.size = r.u32(o + 8); s.info = r.u8(o + 12); s.shndx = r.u16(o + 14);
        }
        if (strtab && st_name) {
            try { s.name = r.cstr(*strtab + st_name); } catch (const ParseError&) {}
        }
        out.push_back(std::move(s));
    }
    return out;
}

bool contains_bytes(const Bytes& d, std::string_view needle) {
    return std::search(d.begin(), d.end(), needle.begin(), needle.end()) != d.end();
}

}  // namespace

bool is_elf(const Bytes& d) { return d.size() >= 4 && d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' && d[3] == 'F'; }

Binary load_elf(const std::string& path, std::shared_ptr<const Bytes> data) {
    const Bytes& d = *data;
    if (d.size() < 0x34) throw ParseError("ELF hlavička je neúplná", 0);
    uint8_t cls = d[4], enc = d[5];
    if (cls != 1 && cls != 2) throw ParseError("neznáma trieda ELF (" + std::to_string(cls) + ")", 4);
    bool is64 = cls == 2;
    bool big = enc == 2;
    Reader r(d, big);
    uint16_t e_type = r.u16(16), e_machine = r.u16(18);
    uint64_t e_entry, e_phoff, e_shoff;
    uint16_t e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
    if (is64) {
        r.need(16, 48);
        e_entry = r.u64(24); e_phoff = r.u64(32); e_shoff = r.u64(40);
        e_phentsize = r.u16(54); e_phnum = r.u16(56); e_shentsize = r.u16(58); e_shnum = r.u16(60); e_shstrndx = r.u16(62);
    } else {
        r.need(16, 36);
        e_entry = r.u32(24); e_phoff = r.u32(28); e_shoff = r.u32(32);
        e_phentsize = r.u16(42); e_phnum = r.u16(44); e_shentsize = r.u16(46); e_shnum = r.u16(48); e_shstrndx = r.u16(50);
    }
    Binary b;
    b.path = path;
    b.data = data;
    b.format = "ELF";
    auto mit = MACHINES.find(e_machine);
    b.arch = mit != MACHINES.end() ? mit->second.first : "x86-64";
    b.bits = mit != MACHINES.end() ? mit->second.second : (is64 ? 64 : 32);
    b.endian = big ? "big" : "little";
    if (e_entry) b.entry = e_entry;
    b.os = "Linux/Unix";
    static const std::map<uint16_t, std::string> TYPES = {
        {1, "REL (objekt)"}, {2, "EXEC (spustiteľný)"}, {3, "DYN (PIE/knižnica)"}, {4, "CORE"}};
    b.meta["elf_type"] = TYPES.count(e_type) ? TYPES.at(e_type) : std::to_string(e_type);
    if (mit == MACHINES.end())
        b.warn("Architektúra ELF (e_machine=" + std::to_string(e_machine) +
                   ") nie je podporovaná – disassembler použije x86-64 ako odhad.", 18);

    // --- program headers (segments) ---
    struct Phdr { uint32_t type, flags; uint64_t off, vaddr, filesz, memsz; };
    std::vector<Phdr> segs;
    try {
        for (int i = 0; i < std::min<int>(e_phnum, 512); i++) {
            uint64_t o = e_phoff + uint64_t(i) * e_phentsize;
            Phdr p{};
            if (is64) {
                r.need(o, 56);
                p.type = r.u32(o); p.flags = r.u32(o + 4); p.off = r.u64(o + 8); p.vaddr = r.u64(o + 16);
                p.filesz = r.u64(o + 32); p.memsz = r.u64(o + 40);
            } else {
                r.need(o, 32);
                p.type = r.u32(o); p.off = r.u32(o + 4); p.vaddr = r.u32(o + 8); p.filesz = r.u32(o + 16);
                p.memsz = r.u32(o + 20); p.flags = r.u32(o + 24);
            }
            segs.push_back(p);
        }
    } catch (const ParseError& e) {
        b.warn("Tabuľka segmentov (program headers) je poškodená – načítal som, čo sa dalo.", e.offset, e.what());
    }
    std::vector<Phdr> loads;
    for (auto& s : segs)
        if (s.type == PT_LOAD) loads.push_back(s);
    if (!loads.empty()) {
        uint64_t m = loads[0].vaddr;
        for (auto& s : loads) m = std::min(m, s.vaddr);
        b.base = m & ~uint64_t(0xFFF);
    }

    // --- section headers ---
    std::vector<Shdr> shdrs;
    try {
        if (e_shoff) {
            for (int i = 0; i < std::min<int>(e_shnum, 4096); i++) {
                uint64_t o = e_shoff + uint64_t(i) * e_shentsize;
                Shdr s{};
                if (is64) {
                    r.need(o, 64);
                    s.name = r.u32(o); s.type = r.u32(o + 4); s.flags = r.u64(o + 8); s.addr = r.u64(o + 16);
                    s.offset = r.u64(o + 24); s.size = r.u64(o + 32); s.link = r.u32(o + 40); s.info = r.u32(o + 44);
                } else {
                    r.need(o, 40);
                    s.name = r.u32(o); s.type = r.u32(o + 4); s.flags = r.u32(o + 8); s.addr = r.u32(o + 12);
                    s.offset = r.u32(o + 16); s.size = r.u32(o + 20); s.link = r.u32(o + 24); s.info = r.u32(o + 28);
                }
                shdrs.push_back(s);
            }
        }
    } catch (const ParseError& e) {
        b.warn("Tabuľka sekcií je poškodená alebo orezaná – použijem segmenty.", e.offset, e.what());
    }
    std::optional<uint64_t> names_off;
    if (e_shstrndx < shdrs.size()) names_off = shdrs[e_shstrndx].offset;
    auto sec_name = [&](size_t idx) -> std::string {
        std::string fallback = "sec" + std::to_string(idx);
        if (!names_off) return fallback;
        try {
            auto n = r.cstr(*names_off + shdrs[idx].name);
            return n.empty() ? fallback : n;
        } catch (const ParseError&) {
            return fallback;
        }
    };

    for (size_t i = 0; i < shdrs.size(); i++) {
        const auto& sh = shdrs[i];
        if (!(sh.flags & SHF_ALLOC) || sh.addr == 0) continue;
        uint64_t fsize = sh.type == SHT_NOBITS ? 0 : sh.size;
        if (fsize && (sh.offset > d.size() || fsize > d.size() - sh.offset)) {
            b.warn("Sekcia " + sec_name(i) + " presahuje koniec súboru – zvyšok bude prázdny.", sh.offset);
            fsize = sh.offset < d.size() ? d.size() - sh.offset : 0;
        }
        b.sections.push_back({sec_name(i), sh.addr, sh.size, sh.offset, fsize, true, bool(sh.flags & SHF_WRITE),
                              bool(sh.flags & SHF_EXEC)});
    }
    if (b.sections.empty()) {  // stripped section table: fall back to segments
        int n = 0;
        for (auto& p : loads) {
            uint64_t fsize = p.off < d.size() ? std::min<uint64_t>(p.filesz, d.size() - p.off) : 0;
            b.sections.push_back({"segment" + std::to_string(n++), p.vaddr, p.memsz, p.off, fsize, bool(p.flags & 4),
                                  bool(p.flags & 2), bool(p.flags & 1)});
        }
        if (!loads.empty())
            b.warn("Súbor nemá tabuľku sekcií – zobrazujem segmenty.",
                   e_shoff ? std::optional<uint64_t>(e_shoff) : std::nullopt, "", "info");
    }
    for (auto& p : loads)
        b.segments.push_back({p.vaddr, p.memsz, p.off, p.filesz,
                              std::string(p.flags & 4 ? "r" : "-") + (p.flags & 2 ? "w" : "-") + (p.flags & 1 ? "x" : "-")});

    // --- symbols ---
    std::vector<Sym> dynsyms;
    for (size_t i = 0; i < shdrs.size(); i++) {
        const auto& sh = shdrs[i];
        if (sh.type != SHT_SYMTAB && sh.type != SHT_DYNSYM) continue;
        std::vector<Sym> syms;
        try {
            syms = read_symbols(r, is64, sh, shdrs);
        } catch (const ParseError& e) {
            b.warn("Tabuľka symbolov " + sec_name(i) + " je poškodená.", e.offset, e.what());
            continue;
        }
        if (sh.type == SHT_DYNSYM) dynsyms = syms;
        std::string source = sh.type == SHT_SYMTAB ? "symtab" : "dynsym";
        for (auto& s : syms) {
            int stype = s.info & 0xF, bind = s.info >> 4;
            if (s.name.empty() || s.shndx == 0 || (stype != 1 && stype != 2) || s.value == 0) continue;
            b.symbols.push_back({s.name, s.value, s.size, stype == 2 ? "func" : "object", source});
            if (sh.type == SHT_DYNSYM && (bind == 1 || bind == 2) && stype == 2) b.exports.push_back({s.name, s.value, {}, ""});
        }
    }

    // --- dynamic: needed libraries ---
    for (auto& sh : shdrs) {
        if (sh.type != SHT_DYNAMIC) continue;
        try {
            std::optional<uint64_t> strtab;
            if (sh.link < shdrs.size()) strtab = shdrs[sh.link].offset;
            uint64_t ent = is64 ? 16 : 8;
            for (uint64_t k = 0; k < std::min<uint64_t>(sh.size / ent, 4096); k++) {
                uint64_t o = sh.offset + k * ent;
                int64_t tag = is64 ? r.i64(o) : r.i32(o);
                uint64_t val = is64 ? r.u64(o + 8) : r.u32(o + 4);
                if (tag == 0) break;
                if (tag == DT_NEEDED && strtab) b.libraries.push_back(r.cstr(*strtab + val));
            }
        } catch (const ParseError& e) {
            b.warn("Sekcia .dynamic je poškodená.", e.offset, e.what());
        }
    }

    // --- relocations -> imports ---
    static const std::map<uint32_t, std::string> none;
    auto nit = RELOC_NAMES.find(b.arch);
    const auto& names = nit != RELOC_NAMES.end() ? nit->second : none;
    std::vector<std::string> import_order;
    std::map<std::string, Import> seen;
    for (size_t i = 0; i < shdrs.size(); i++) {
        const auto& sh = shdrs[i];
        if (sh.type != SHT_RELA && sh.type != SHT_REL) continue;
        try {
            std::vector<Sym> symlist = dynsyms;
            if (sh.link < shdrs.size() && (shdrs[sh.link].type == SHT_SYMTAB || shdrs[sh.link].type == SHT_DYNSYM))
                symlist = read_symbols(r, is64, shdrs[sh.link], shdrs);
            bool rela = sh.type == SHT_RELA;
            uint64_t ent = is64 ? (rela ? 24 : 16) : (rela ? 12 : 8);
            for (uint64_t k = 0; k < std::min<uint64_t>(sh.size / ent, 200000); k++) {
                uint64_t o = sh.offset + k * ent;
                uint64_t off, info, sym_i;
                uint32_t rtype;
                int64_t addend = 0;
                if (is64) {
                    r.need(o, ent);
                    off = r.u64(o); info = r.u64(o + 8); sym_i = info >> 32; rtype = uint32_t(info);
                    if (rela) addend = r.i64(o + 16);
                } else {
                    r.need(o, ent);
                    off = r.u32(o); info = r.u32(o + 4); sym_i = info >> 8; rtype = uint32_t(info & 0xFF);
                    if (rela) addend = r.i32(o + 8);
                }
                std::string symname = (sym_i > 0 && sym_i < symlist.size()) ? symlist[sym_i].name : "";
                auto tn = names.find(rtype);
                std::string tname = tn != names.end() ? tn->second : std::to_string(rtype);
                b.relocations.push_back({off, tname, symname, addend});
                bool jump = contains(tname, "JUMP_SLOT") || contains(tname, "JMP_SLOT");
                if (!symname.empty() && (jump || contains(tname, "GLOB_DAT")) && symlist[sym_i].shndx == 0) {
                    auto it = seen.find(symname);
                    if (it == seen.end()) {
                        import_order.push_back(symname);
                        seen[symname] = {symname, "", off, {}, {}};
                    } else if (jump) {
                        it->second = {symname, "", off, {}, {}};
                    }
                }
            }
        } catch (const ParseError& e) {
            b.warn("Relokácie v " + sec_name(i) + " sú poškodené.", e.offset, e.what());
        }
    }
    for (auto& n : import_order) b.imports.push_back(seen[n]);

    // --- compiler ---
    bool found = false;
    for (size_t i = 0; i < shdrs.size() && !found; i++) {
        std::string n = sec_name(i);
        if (n == ".comment") {
            try {
                std::string text = r.raw(shdrs[i].offset, std::min<uint64_t>(shdrs[i].size, 512));
                for (auto& c : text) if (c == '\0') c = ' ';
                text = trim(utf8_lossy(text));
                if (!text.empty()) { b.compiler = text; found = true; }
            } catch (const ParseError&) {}
        } else if (n == ".note.go.buildid") {
            b.compiler = "Go";
            found = true;
        }
    }
    if (!found) {
        if (contains_bytes(d, "rust_panic") || contains_bytes(d, "/rustc/")) b.compiler = "Rust (rustc)";
        else if (contains_bytes(d, "GCC: (")) b.compiler = "GCC";
    }
    return b;
}

}  // namespace rv
