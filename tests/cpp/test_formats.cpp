// Format loaders, raw guessing, reader bounds, graceful degradation and the disassembler.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "core/errors.hpp"
#include "core/reader.hpp"
#include "disasm/disasm.hpp"
#include "formats/formats.hpp"
#include "test.hpp"

using namespace rv;
namespace fs = std::filesystem;

static Bytes unhex(const std::string& h) {
    Bytes out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}
static Bytes repeat(const Bytes& b, int n) {
    Bytes out;
    for (int i = 0; i < n; i++) out.insert(out.end(), b.begin(), b.end());
    return out;
}
static Bytes file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
static std::shared_ptr<const Bytes> shared(Bytes b) { return std::make_shared<const Bytes>(std::move(b)); }
static bool has_error(const Binary& b) {
    for (auto& d : b.diagnostics)
        if (d.severity == "error") return true;
    return false;
}
static std::set<std::string> import_names(const Binary& b) {
    std::set<std::string> s;
    for (auto& i : b.imports) s.insert(i.name);
    return s;
}
static bool includes(const std::set<std::string>& have, const std::set<std::string>& want) {
    for (auto& w : want)
        if (!have.count(w)) return false;
    return true;
}

TEST("headers of the hello samples") {
    struct E { const char* name; const char* fmt; const char* arch; int bits; Addr entry; };
    for (auto e : {E{"hello_x64.elf", "ELF", "x86-64", 64, 0x401070}, E{"hello_arm64.elf", "ELF", "arm64", 64, 0x700},
                   E{"hello_x64.exe", "PE", "x86-64", 64, 0x140001410}, E{"hello_x86.exe", "PE", "x86", 32, 0x4013E0}}) {
        auto b = load_file(rvtest::sample(e.name));
        CHECK_EQ(b.format, std::string(e.fmt));
        CHECK_EQ(b.arch, std::string(e.arch));
        CHECK_EQ(b.bits, e.bits);
        CHECK(b.entry == e.entry);
        CHECK_EQ(b.endian, std::string("little"));
        bool exec = false;
        for (auto& s : b.sections) exec |= s.executable;
        CHECK(exec);
        CHECK(!has_error(b));
    }
}

TEST("ELF details") {
    auto b = load_file(rvtest::sample("hello_x64.elf"));
    std::set<std::string> names;
    for (auto& s : b.sections) names.insert(s.name);
    CHECK(includes(names, {".text", ".rodata", ".data", ".bss", ".plt"}));
    CHECK(includes(import_names(b), {"printf", "puts", "__libc_start_main"}));
    CHECK(std::find(b.libraries.begin(), b.libraries.end(), "libc.so.6") != b.libraries.end());
    bool sum_to = false, jump_slot = false;
    for (auto& s : b.symbols) sum_to |= s.name == "sum_to" && s.kind == "func";
    for (auto& r : b.relocations) jump_slot |= r.type.find("JUMP_SLOT") != std::string::npos;
    CHECK(sum_to);
    CHECK(jump_slot);
    CHECK(b.compiler.rfind("GCC", 0) == 0);
}

TEST("PE details") {
    auto b = load_file(rvtest::sample("login_x64.exe"));
    CHECK(includes(import_names(b), {"strcmp", "puts"}));
    bool dll = false, dir64 = false;
    for (auto& l : b.libraries) dll |= l.size() > 4 && (l.substr(l.size() - 4) == ".dll" || l.substr(l.size() - 4) == ".DLL");
    for (auto& r : b.relocations) dir64 |= r.type == "DIR64";
    CHECK(dll);
    CHECK(dir64);
    CHECK(!b.extra_function_starts.empty());  // .pdata
    CHECK(b.compiler.rfind("MinGW", 0) == 0);
    CHECK_EQ(b.meta["subsystem"], std::string("konzola"));
    auto* sec = b.section_at(*b.entry);
    CHECK(sec && sec->executable);
}

TEST("stripped ELF keeps imports") {
    auto b = load_file(rvtest::sample("login_x64_stripped.elf"));
    for (auto& s : b.symbols) CHECK(s.source != "symtab");
    CHECK(includes(import_names(b), {"strcmp", "puts"}));
}

TEST("address mapping roundtrip") {
    auto b = load_file(rvtest::sample("hello_x64.elf"));
    const Section *text = nullptr, *bss = nullptr;
    for (auto& s : b.sections) {
        if (s.name == ".text") text = &s;
        if (s.name == ".bss") bss = &s;
    }
    CHECK(text && bss);
    auto off = b.addr_to_offset(text->addr + 5);
    CHECK(off == text->offset + 5);
    CHECK(b.offset_to_addr(*off) == text->addr + 5);
    CHECK(b.read(bss->addr, 4) == Bytes(4, 0));
    CHECK(!b.addr_to_offset(bss->addr).has_value());
    CHECK(b.read(0x10, 4).empty());  // unmapped
}

TEST("format detection and raw arch guess") {
    auto pad = [](Bytes b) { b.resize(b.size() + 60, 0); return b; };
    CHECK_EQ(detect_format(pad({0x7f, 'E', 'L', 'F'})), std::string("ELF"));
    CHECK_EQ(detect_format(pad({0xcf, 0xfa, 0xed, 0xfe})), std::string("MACHO"));
    CHECK_EQ(detect_format(Bytes{'P', 'K', 3, 4}), std::string("ARCHIVE"));
    CHECK_EQ(detect_format(Bytes(100, 0x90)), std::string("RAW"));
    Bytes code = repeat(unhex("554889e5897dfc8b45fc5dc3"), 4);
    CHECK_EQ(guess_arch(code), std::string("x86-64"));
    CHECK_EQ(guess_arch(repeat(unhex("fd7bbfa9fd030091fd7bc1a8c0035fd6"), 4)), std::string("arm64"));
    LoadOptions o;
    o.base = 0x1000;
    auto raw = load_bytes(shared(code), "shellcode.bin", o);
    CHECK(raw.format == "RAW" && raw.arch == "x86-64" && raw.entry == Addr(0x1000));
    bool guessed = false;
    for (auto& d : raw.diagnostics) guessed |= d.message.find("odhadol") != std::string::npos;
    CHECK(guessed);
    LoadOptions m;
    m.arch = "x86";
    m.base = 0x8000;
    auto manual = load_bytes(shared(code), "x.bin", m);
    CHECK(manual.arch == "x86" && manual.bits == 32 && manual.diagnostics.empty());
}

TEST("Mach-O and ZIP fall back to raw") {
    Bytes macho{0xcf, 0xfa, 0xed, 0xfe};
    macho.resize(68, 0);
    auto b = load_bytes(shared(macho), "a.macho");
    CHECK(b.format == "RAW" && b.diagnostics.back().message.find("Mach-O") != std::string::npos);
    Bytes zip{'P', 'K', 3, 4};
    zip.resize(68, 0);
    auto z = load_bytes(shared(zip), "a.apk");
    CHECK(z.diagnostics.back().message.find("APK") != std::string::npos);
}

TEST("load errors") {
    auto dir = fs::temp_directory_path() / ("rvtest-load-" + std::to_string(std::rand()));
    fs::create_directories(dir);
    auto throws = [](const std::string& p, const char* text) {
        try {
            load_file(p);
            return false;
        } catch (const LoadError& e) {
            return std::string(e.what()).find(text) != std::string::npos;
        }
    };
    CHECK(throws((dir / "missing.bin").string(), ""));
    CHECK(throws(dir.string(), ""));
    std::ofstream((dir / "empty.bin").string()).close();
    CHECK(throws((dir / "empty.bin").string(), "prázdny"));
    fs::remove_all(dir);
}

TEST("reader bounds") {
    Bytes d{1, 2, 3};
    Reader r(d);
    CHECK_EQ(r.u16(0), uint16_t(0x0201));
    auto throws = [](auto f) {
        try {
            f();
            return false;
        } catch (const ParseError&) {
            return true;
        }
    };
    CHECK(throws([&] { r.u32(0); }));
    CHECK(throws([&] { r.raw(2, 5); }));
    CHECK(throws([&] { r.cstr(10); }));
    CHECK_EQ(r.cstr(0), std::string("\x01\x02\x03"));
}

TEST("corrupted ELF header falls back to raw") {
    Bytes data = file_bytes(rvtest::sample("hello_x64.elf"));
    data[4] = 7;  // invalid ELF class
    auto b = load_bytes(shared(data), "bad.elf");
    CHECK_EQ(b.format, std::string("RAW"));
    const Diagnostic* err = nullptr;
    for (auto& d : b.diagnostics)
        if (d.severity == "error" && !err) err = &d;
    CHECK(err && err->message.find("poškodený") != std::string::npos && !err->detail.empty());
}

TEST("truncated PE reports but loads") {
    Bytes data = file_bytes(rvtest::sample("login_x64.exe"));
    data.resize(0x2000);
    auto b = load_bytes(shared(data), "trunc.exe");
    CHECK_EQ(b.format, std::string("PE"));
    bool reported = false;
    for (auto& d : b.diagnostics)
        reported |= d.message.find("koniec súboru") != std::string::npos || d.message.find("poškodené") != std::string::npos;
    CHECK(reported);
}

// --- disassembler ---

static Insn one(const Disassembler& d, const std::string& hex, Addr at) {
    Bytes b = unhex(hex);
    auto i = d.decode_one(b.data(), b.size(), at);
    CHECK(i.has_value());
    return i ? *i : Insn{};
}
static bool has_ref(const Insn& i, Addr a, const char* kind) {
    return std::find(i.refs.begin(), i.refs.end(), Ref{a, kind}) != i.refs.end();
}

TEST("x86-64 flow and references") {
    Disassembler d("x86-64", "little", [](Addr a) { return a >= 0x400000 && a < 0x500000; });
    Bytes code = unhex(
        "e800000000"      // call +0
        "ff1500100000"    // call [rip+0x1000]
        "488d0500100000"  // lea rax, [rip+0x1000]
        "7405"            // je
        "eb00"            // jmp
        "c3"              // ret
        "bf00104000"      // mov edi, 0x401000 (immediate into mapped memory)
        "f4");            // hlt
    auto ins = d.decode(code.data(), code.size(), 0x401000);
    std::vector<std::string> flows;
    for (auto& i : ins) flows.push_back(flow_name(i.flow));
    CHECK(flows == std::vector<std::string>({"call", "icall", "seq", "cjmp", "jmp", "ret", "seq", "trap"}));
    if (ins.size() != 8) return;
    CHECK(ins[0].target == Addr(0x401005));
    CHECK(ins[1].mem_slot == Addr(0x401005 + 6 + 0x1000));
    CHECK(has_ref(ins[2], 0x40100B + 7 + 0x1000, "addr"));
    CHECK(ins[3].target == ins[3].end() + 5);
    CHECK(has_ref(ins[6], 0x401000, "addr"));
}

TEST("x86 memory access kinds and 32-bit absolute") {
    Disassembler d("x86-64");
    CHECK(one(d, "890500100000", 0x1000).refs == std::vector<Ref>({{0x2006, "write"}}));  // mov [rip+0x1000], eax
    CHECK(one(d, "8b0500100000", 0x1000).refs == std::vector<Ref>({{0x2006, "read"}}));   // mov eax, [rip+0x1000]
    Disassembler d32("x86");
    CHECK(has_ref(one(d32, "a100104000", 0x401000), 0x401000, "read"));  // mov eax, [0x401000]
}

TEST("arm64 adrp tracking and flow") {
    Disassembler d("arm64");
    RefTracker t;
    // adrp x16, 0x1f000 ; ldr x17, [x16, #0xf98] ; add x16, x16, #0xf98 ; br x17
    Bytes code = unhex("f00000b011ce47f910623e9120021fd6");
    auto ins = d.decode(code.data(), code.size(), 0x650, 0, &t);
    CHECK_EQ(ins.size(), size_t(4));
    if (ins.size() != 4) return;
    CHECK(ins[3].flow == Flow::IJmp);
    CHECK(!ins[1].refs.empty() && ins[1].refs[0].kind == "read");
    CHECK(!ins[1].refs.empty() && ins[3].mem_slot == ins[1].refs[0].addr);
    auto bl = one(d, "05000094", 0x1000);  // bl +0x14
    CHECK(bl.flow == Flow::Call && bl.target == Addr(0x1014));
    CHECK(one(d, "c0035fd6", 0).flow == Flow::Ret);
    auto cb = one(d, "400000b4", 0x100);  // cbz x0, +8
    CHECK(cb.flow == Flow::CJmp && cb.target == Addr(0x108));
}

TEST("arm32 generic flow and unknown arch") {
    Disassembler d("arm");
    CHECK(one(d, "010000eb", 0x8000).flow == Flow::Call);  // bl
    CHECK(one(d, "1eff2fe1", 0x8000).flow == Flow::Ret);   // bx lr
    bool threw = false;
    try {
        Disassembler bad("mips");
    } catch (const ReversaError&) {
        threw = true;
    }
    CHECK(threw);
}
