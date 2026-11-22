// PE/COFF loader (PE32 and PE32+; x86, x86-64, ARM64) from the Microsoft "PE Format" specification:
// headers, sections, imports, exports, base relocations, x64 .pdata and MinGW COFF symbols.
#include <functional>
#include <algorithm>
#include <map>
#include <set>

#include "core/reader.hpp"
#include "formats/formats.hpp"

namespace rv {
namespace {

const std::map<uint16_t, std::pair<std::string, int>> MACHINES = {
    {0x14C, {"x86", 32}}, {0x8664, {"x86-64", 64}}, {0xAA64, {"arm64", 64}}, {0x1C0, {"arm", 32}}, {0x1C4, {"arm", 32}}};
constexpr uint32_t SCN_CODE = 0x20, SCN_EXEC = 0x20000000, SCN_READ = 0x40000000, SCN_WRITE = 0x80000000;

struct Dir { uint32_t rva, size; };
using RvaOff = std::function<uint64_t(uint64_t)>;

bool contains_bytes(const Bytes& d, std::string_view needle, size_t from = 0, size_t to = SIZE_MAX) {
    to = std::min(to, d.size());
    if (from >= to) return false;
    return std::search(d.begin() + from, d.begin() + to, needle.begin(), needle.end()) != d.begin() + to;
}

void imports(Binary& b, const Reader& r, const std::vector<Dir>& dirs, const RvaOff& rva_off, bool plus) {
    auto [rva, size] = dirs[1];
    if (!rva) return;
    uint64_t o = rva_off(rva);
    int ptr = plus ? 8 : 4;
    uint64_t flag = plus ? (uint64_t(1) << 63) : (uint64_t(1) << 31);
    for (int n = 0; n < 4096; n++) {
        r.need(o, 20);
        uint32_t ilt = r.u32(o), name_rva = r.u32(o + 12), iat = r.u32(o + 16);
        if (!(ilt || name_rva || iat)) break;
        std::string lib = name_rva ? r.cstr(rva_off(name_rva)) : "?";
        b.libraries.push_back(lib);
        uint32_t lookup = ilt ? ilt : iat;
        for (uint64_t k = 0; k < 65536; k++) {
            uint64_t entry = r.uint(rva_off(lookup) + k * ptr, ptr);
            if (entry == 0) break;
            Addr slot = b.base + iat + k * ptr;
            if (entry & flag) {
                uint32_t ordn = uint32_t(entry & 0xFFFF);
                b.imports.push_back({split(lib, '.')[0] + "_ord" + std::to_string(ordn), lib, slot, {}, ordn});
            } else {
                b.imports.push_back({r.cstr(rva_off(entry & 0x7FFFFFFF) + 2), lib, slot, {}, {}});
            }
        }
        o += 20;
    }
}

void exports(Binary& b, const Reader& r, const std::vector<Dir>& dirs, const RvaOff& rva_off, bool) {
    auto [rva, size] = dirs[0];
    if (!rva) return;
    uint64_t o = rva_off(rva);
    r.need(o, 40);
    uint32_t base = r.u32(o + 16), nfuncs = r.u32(o + 20), nnames = r.u32(o + 24);
    uint32_t funcs_rva = r.u32(o + 28), names_rva = r.u32(o + 32), ords_rva = r.u32(o + 36);
    std::vector<uint32_t> funcs;
    for (uint32_t i = 0; i < std::min<uint32_t>(nfuncs, 65536); i++) funcs.push_back(r.u32(rva_off(funcs_rva) + 4 * i));
    std::map<uint32_t, std::string> named;
    for (uint32_t i = 0; i < std::min<uint32_t>(nnames, 65536); i++) {
        uint32_t nrva = r.u32(rva_off(names_rva) + 4 * i);
        uint16_t ordi = r.u16(rva_off(ords_rva) + 2 * i);
        named[ordi] = r.cstr(rva_off(nrva));
    }
    for (uint32_t i = 0; i < funcs.size(); i++) {
        uint32_t frva = funcs[i];
        if (!frva) continue;
        std::string name = named.count(i) ? named[i] : "ord_" + std::to_string(base + i);
        std::string fwd;
        if (rva <= frva && frva < uint64_t(rva) + size) fwd = r.cstr(rva_off(frva));
        b.exports.push_back({name, b.base + frva, base + i, fwd});
        if (fwd.empty())
            b.symbols.push_back({name, b.base + frva, 0, b.is_code(b.base + frva) ? "func" : "object", "export"});
    }
}

void relocs(Binary& b, const Reader& r, const std::vector<Dir>& dirs, const RvaOff& rva_off, bool) {
    auto [rva, size] = dirs[5];
    if (!rva) return;
    uint64_t o = rva_off(rva), end = o + size;
    static const std::map<int, std::string> TYPES = {{3, "HIGHLOW"}, {10, "DIR64"}, {0, "ABSOLUTE"}};
    while (o + 8 <= end && b.relocations.size() < 500000) {
        uint32_t page = r.u32(o), bsize = r.u32(o + 4);
        if (bsize < 8) break;
        for (uint32_t k = 0; k < (bsize - 8) / 2; k++) {
            uint16_t v = r.u16(o + 8 + 2 * k);
            int t = v >> 12;
            if (t) b.relocations.push_back({b.base + page + (v & 0xFFF), TYPES.count(t) ? TYPES.at(t) : std::to_string(t), "", 0});
        }
        o += bsize;
    }
}

void pdata(Binary& b, const Reader& r, const std::vector<Dir>& dirs, const RvaOff& rva_off, bool) {
    auto [rva, size] = dirs[3];
    if (!rva || b.arch != "x86-64") return;
    uint64_t o = rva_off(rva);
    for (uint32_t k = 0; k < std::min<uint32_t>(size / 12, 200000); k++) {
        uint32_t start = r.u32(o + 12 * k), end = r.u32(o + 12 * k + 4);
        if (start && end > start) b.extra_function_starts.push_back(b.base + start);
    }
}

void coff_symbols(Binary& b, const Reader& r, uint32_t symptr, uint32_t nsyms) {
    uint64_t strtab = symptr + uint64_t(nsyms) * 18;
    std::set<Addr> seen;
    for (auto& s : b.symbols) seen.insert(s.addr);
    uint32_t i = 0;
    while (i < std::min<uint32_t>(nsyms, 500000)) {
        uint64_t o = symptr + uint64_t(i) * 18;
        std::string raw = r.raw(o, 8);
        uint32_t value = r.u32(o + 8);
        int16_t secnum = r.i16(o + 12);
        uint16_t typ = r.u16(o + 14);
        uint8_t sclass = r.u8(o + 16), naux = r.u8(o + 17);
        std::string name;
        if (raw.compare(0, 4, std::string(4, '\0')) == 0) {
            name = r.cstr(strtab + r.u32(o + 4));
        } else {
            name = latin1_to_utf8(raw.substr(0, raw.find('\0')));
        }
        if (secnum > 0 && size_t(secnum) <= b.sections.size() && (sclass == 2 || sclass == 3) && !name.empty() && name[0] != '.') {
            Addr addr = b.sections[secnum - 1].addr + value;
            bool is_func = (typ >> 4) == 2 || typ == 0x20;
            if (is_func && !seen.count(addr)) {
                b.symbols.push_back({name, addr, 0, "func", "coff"});
                seen.insert(addr);
            }
        }
        i += 1 + naux;
    }
}

}  // namespace

bool is_pe(const Bytes& d) {
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    uint64_t lfanew = uint64_t(d[0x3C]) | uint64_t(d[0x3D]) << 8 | uint64_t(d[0x3E]) << 16 | uint64_t(d[0x3F]) << 24;
    return lfanew + 4 <= d.size() && d[lfanew] == 'P' && d[lfanew + 1] == 'E' && d[lfanew + 2] == 0 && d[lfanew + 3] == 0;
}

Binary load_pe(const std::string& path, std::shared_ptr<const Bytes> data) {
    const Bytes& d = *data;
    Reader r(d);
    uint32_t lfanew = r.u32(0x3C);
    if (r.raw(lfanew, 4) != std::string("PE\0\0", 4)) throw ParseError("chýba podpis PE", lfanew);
    r.need(lfanew + 4, 20);
    uint16_t machine = r.u16(lfanew + 4), nsec = r.u16(lfanew + 6);
    uint32_t symptr = r.u32(lfanew + 12), nsyms = r.u32(lfanew + 16);
    uint16_t opt_size = r.u16(lfanew + 20), chars = r.u16(lfanew + 22);
    auto mit = MACHINES.find(machine);
    uint64_t opt = uint64_t(lfanew) + 24;
    uint16_t magic = r.u16(opt);
    if (magic != 0x10B && magic != 0x20B) throw ParseError("neznámy typ voliteľnej hlavičky (" + hex(magic) + ")", opt);
    bool plus = magic == 0x20B;
    uint32_t entry_rva = r.u32(opt + 16);
    uint64_t image_base = plus ? r.u64(opt + 24) : r.u32(opt + 28);
    uint16_t subsystem = r.u16(opt + 68);
    uint32_t ndirs = r.u32(opt + (plus ? 108 : 92));
    uint64_t dirs_off = opt + (plus ? 112 : 96);

    Binary b;
    b.path = path;
    b.data = data;
    b.format = "PE";
    if (mit != MACHINES.end()) { b.arch = mit->second.first; b.bits = mit->second.second; }
    else { b.arch = plus ? "x86-64" : "x86"; b.bits = plus ? 64 : 32; }
    b.base = image_base;
    if (entry_rva) b.entry = image_base + entry_rva;
    b.os = "Windows";
    if (mit == MACHINES.end()) b.warn("Neznámy typ procesora PE (" + hex(machine) + ") – odhadujem " + b.arch + ".", lfanew + 4);
    static const std::map<uint16_t, std::string> SUBS = {{1, "native"}, {2, "GUI"}, {3, "konzola"}, {10, "EFI"}};
    b.meta["subsystem"] = SUBS.count(subsystem) ? SUBS.at(subsystem) : std::to_string(subsystem);
    b.meta["dll"] = (chars & 0x2000) ? "áno" : "nie";

    std::vector<Dir> dirs;
    for (uint32_t i = 0; i < std::min<uint32_t>(ndirs, 16); i++) {
        try { dirs.push_back({r.u32(dirs_off + i * 8), r.u32(dirs_off + i * 8 + 4)}); }
        catch (const ParseError&) { dirs.push_back({0, 0}); }
    }
    while (dirs.size() < 16) dirs.push_back({0, 0});

    uint64_t sec_off = opt + opt_size;
    std::optional<uint64_t> strtab_off;
    if (symptr) strtab_off = symptr + uint64_t(nsyms) * 18;
    for (int i = 0; i < std::min<int>(nsec, 96); i++) {
        uint64_t o = sec_off + uint64_t(i) * 40;
        std::string raw_name;
        uint32_t vsize, va, rawsize, rawptr, flags;
        try {
            r.need(o, 40);
            raw_name = r.raw(o, 8);
            vsize = r.u32(o + 8); va = r.u32(o + 12); rawsize = r.u32(o + 16); rawptr = r.u32(o + 20); flags = r.u32(o + 36);
        } catch (const ParseError& e) {
            b.warn("Tabuľka sekcií PE je orezaná.", e.offset, e.what());
            break;
        }
        std::string name = latin1_to_utf8(raw_name.substr(0, raw_name.find('\0')));
        if (!name.empty() && name[0] == '/' && strtab_off) {  // long name in COFF string table
            try { name = r.cstr(*strtab_off + std::stoull(name.substr(1))); } catch (...) {}
        }
        uint64_t size = vsize ? vsize : rawsize;
        uint64_t fsize = std::min<uint64_t>(rawsize, size);
        if (uint64_t(rawptr) + fsize > d.size()) {
            b.warn("Sekcia " + name + " presahuje koniec súboru – zvyšok bude prázdny.", rawptr);
            fsize = rawptr < d.size() ? d.size() - rawptr : 0;
        }
        b.sections.push_back({name, image_base + va, size, rawptr, fsize, bool(flags & SCN_READ), bool(flags & SCN_WRITE),
                              bool(flags & (SCN_EXEC | SCN_CODE))});
    }

    RvaOff rva_off = [&](uint64_t rva) -> uint64_t {
        auto off = b.addr_to_offset(image_base + rva);
        if (!off) throw ParseError("RVA " + hex(rva) + " neukazuje do súboru", rva);
        return *off;
    };
    using Fn = void (*)(Binary&, const Reader&, const std::vector<Dir>&, const RvaOff&, bool);
    const std::pair<const char*, Fn> parts[] = {
        {"Importy", imports}, {"Exporty", exports}, {"Relokácie", relocs}, {"Tabuľka výnimiek", pdata}};
    for (auto& [label, fn] : parts) {
        try { fn(b, r, dirs, rva_off, plus); }
        catch (const ParseError& e) { b.warn(std::string(label) + ": dáta sú poškodené – načítal som, čo sa dalo.", e.offset, e.what()); }
    }
    if (symptr && nsyms) {
        try { coff_symbols(b, r, symptr, nsyms); }
        catch (const ParseError& e) { b.warn("Tabuľka COFF symbolov je poškodená.", e.offset, e.what()); }
    }

    // --- compiler / packer ---
    bool crt = false, upx = false;
    for (auto& s : b.sections) {
        if (s.name == ".CRT" || s.name == ".tls") crt = true;
        if (starts_with(s.name, "UPX")) upx = true;
    }
    Bytes low(d);
    for (auto& c : low) c = uint8_t(std::tolower(c));
    if (contains_bytes(d, "Rich", 0x40, lfanew)) b.compiler = "Microsoft Visual C++ (Rich hlavička)";
    else if (crt && (contains_bytes(low, "mingw") || contains_bytes(d, "GCC: ("))) b.compiler = "MinGW-w64 GCC";
    else if (contains_bytes(d, "Go build ID")) b.compiler = "Go";
    else if (contains_bytes(d, "rust_panic")) b.compiler = "Rust (rustc)";
    if (upx) b.warn("Súbor je pravdepodobne skomprimovaný packerom UPX – kód bude čitateľný až po rozbalení.", std::nullopt, "", "info");
    return b;
}

}  // namespace rv
