// Auto-analysis internals (thunks, noreturn, CFG, call graph, xrefs, search) and parser fuzzing.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>

#include "analysis/analysis.hpp"
#include "core/errors.hpp"
#include "formats/formats.hpp"
#include "session/session.hpp"
#include "test.hpp"

using namespace rv;

static Bytes file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}

static void analyze_bytes(Bytes data, const std::string& name) {
    auto b = load_bytes(std::make_shared<const Bytes>(std::move(data)), name);
    auto a = analyze(b);
    CHECK(a->done);
}

static Addr addr_of(Session& s, const std::string& name) {
    auto a = s.resolve(name);
    CHECK(a.has_value());
    return a.value_or(0);
}

static std::set<std::string> xref_functions(Session& s, Addr to, const std::string& kind) {
    std::set<std::string> out;
    for (auto& x : s.analysis->xrefs.to(to, {kind}))
        if (auto* f = s.analysis->function_containing(x.frm)) out.insert(s.name_of(f->addr));
    return out;
}

static const String* find_string(const Analysis& a, const std::string& text) {
    for (auto& s : a.strings)
        if (s.text == text) return &s;
    return nullptr;
}

TEST("string, data pointer and read xrefs") {
    auto s = Session::open(rvtest::sample("login_x64.elf"));
    auto& a = s->require_analysis();
    auto* admin = find_string(a, "admin");
    CHECK(admin != nullptr);
    if (admin) {
        auto xs = a.xrefs.to(admin->addr);
        CHECK(!xs.empty() && (xs[0].kind == "addr" || xs[0].kind == "read"));
        CHECK(!xs.empty() && s->name_of(a.function_containing(xs[0].frm)->addr) == "check_user");
    }
    auto h = Session::open(rvtest::sample("hello_x64.elf"));
    auto& ha = h->require_analysis();
    auto* ahoj = find_string(ha, "Ahoj");
    CHECK(ahoj && !ha.xrefs.to(ahoj->addr, {"data"}).empty());  // greetings[] points to the string
    CHECK(xref_functions(*h, addr_of(*h, "greetings"), "read") == std::set<std::string>({"greet"}));
}

TEST("PE import thunks resolve to their import") {
    auto s = Session::open(rvtest::sample("login_x64.exe"));
    auto& a = s->require_analysis();
    std::optional<Addr> thunk;
    for (auto& [t, target] : a.thunk_target)
        if (s->name_of(t) == "strcmp") thunk = t;
    CHECK(thunk.has_value());
    if (!thunk) return;
    CHECK_EQ(a.functions.at(*thunk).kind, std::string("thunk"));
    std::set<std::string> callers;
    for (auto c : a.callers(addr_of(*s, "strcmp"))) callers.insert(s->name_of(c));
    CHECK(callers == std::set<std::string>({"check_password", "check_user"}));
}

TEST("a call to a noreturn function ends the block") {
    auto s = Session::open(rvtest::sample("login_x64.elf"));
    auto& a = s->require_analysis();
    Addr chk = addr_of(*s, "__stack_chk_fail");
    int seen = 0;
    for (auto& [start, blk] : a.functions.at(addr_of(*s, "check_password")).blocks) {
        auto* last = a.insn(blk.insns.back());
        if (last && last->target == chk) {
            seen++;
            CHECK(blk.succs.empty());
        }
    }
    CHECK(seen > 0);
}

TEST("CFG loops, call graph neighbourhood, function_containing") {
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    auto& a = s->require_analysis();
    Addr sum_to = addr_of(*s, "sum_to"), main = addr_of(*s, "main");
    auto g = build_cfg(a.functions.at(sum_to));
    CHECK_EQ(g.loops.size(), size_t(1));
    bool back = false;
    for (auto& e : g.edges) back |= e.back;
    CHECK(back);
    auto g2 = build_cfg(a.functions.at(addr_of(*s, "classify")));
    CHECK(g2.loops.empty() && g2.nodes.size() >= 4);

    auto full = call_graph(a.functions);
    auto sub = call_graph(a.functions, sum_to, 1);
    CHECK(std::set<Addr>(sub.nodes.begin(), sub.nodes.end()) == std::set<Addr>({sum_to, main}));
    CHECK(full.nodes.size() > sub.nodes.size());

    auto* f = a.function_containing(sum_to + 5);
    CHECK(f && f->addr == sum_to);
    CHECK(f && f->size > 10 && f->insn_count > 5);
}

TEST("search: bytes, strings, instructions, pseudocode, errors") {
    auto s = Session::open(rvtest::sample("login_x64.elf"));
    s->require_analysis();
    CHECK(!s->search_bytes("f3 0f 1e fa").empty());  // endbr64
    CHECK(s->search_bytes("f30f1efa") == s->search_bytes("f3 0f 1e fa"));
    CHECK(!s->search_bytes("f3 ?? 1e fa").empty());
    CHECK(!s->search_strings("ACCESS").empty());
    CHECK(!s->search_strings("^Usage", true).empty());
    CHECK(!s->search_instructions(R"(call\s+0x[0-9a-f]+)").empty());
    bool strcmp = false;
    for (auto& [ad, line] : s->search_pseudocode(R"(strcmp\()")) strcmp |= line.find("strcmp") != std::string::npos;
    CHECK(strcmp);
    auto throws = [](auto f) {
        try {
            f();
            return false;
        } catch (const ReversaError&) {
            return true;
        }
    };
    CHECK(throws([&] { s->search_bytes("zz"); }));
    CHECK(throws([&] { s->search_bytes(""); }));
    CHECK(throws([&] { search_text({{1, "a"}}, "(", true); }));
}

TEST("raw shellcode analysis") {
    LoadOptions o;
    o.arch = "x86-64";
    o.base = 0x1000;
    auto b = load_bytes(std::make_shared<const Bytes>(Bytes{0x55, 0x48, 0x89, 0xe5, 0xb8, 1, 0, 0, 0, 0x5d, 0xc3}),
                        "sc.bin", o);
    auto a = analyze(b);
    CHECK(b.format == "RAW" && b.entry == Addr(0x1000));
    CHECK(a->functions.size() == 1 && a->functions.count(0x1000));
}

// --- fuzzing: deterministic seeds; REVERSA_FUZZ_ITERS raises the count for long local runs ---

static Bytes mutate(std::mt19937& rng, const Bytes& base) {
    auto rnd = [&](size_t lo, size_t hi) { return std::uniform_int_distribution<size_t>(lo, hi)(rng); };
    auto coin = [&] { return std::uniform_real_distribution<double>(0, 1)(rng); };
    Bytes d = base;
    for (size_t n = rnd(1, 12); n; n--) {
        double op = coin();
        size_t hot = coin() < 0.7 ? rnd(0, std::min<size_t>(d.size() - 1, 0x400)) : rnd(0, d.size() - 1);
        if (op < 0.6) {
            d[hot] = uint8_t(rnd(0, 255));
        } else if (op < 0.8) {
            static const uint8_t magic[] = {0x00, 0xFF, 0x7F, 0x80};
            for (size_t k = 0; k < 4 && hot + k < d.size(); k++) d[hot + k] = magic[rnd(0, 3)];
        } else {
            d.resize(rnd(1, d.size()));  // truncate
        }
    }
    return d;
}

TEST("fuzz: mutated samples never crash") {
    const char* env = std::getenv("REVERSA_FUZZ_ITERS");
    int iters = env ? std::atoi(env) : 60;
    for (auto name : {"login_x64.elf", "login_arm64.elf", "login_x64_stripped.exe", "login_x86.exe"}) {
        Bytes base = file_bytes(rvtest::sample(name));
        std::mt19937 rng(uint32_t(std::hash<std::string>{}(name)));
        for (int i = 0; i < iters; i++) analyze_bytes(mutate(rng, base), "fuzz" + std::to_string(i));
    }
}

TEST("fuzz: truncation at many boundaries") {
    for (auto name : {"hello_x64.elf", "hello_x64.exe"}) {
        Bytes data = file_bytes(rvtest::sample(name));
        std::vector<size_t> cuts;
        for (size_t c = 1; c < 0x200; c += 7) cuts.push_back(c);
        cuts.insert(cuts.end(), {data.size() / 3, data.size() / 2, data.size() - 1});
        for (auto c : cuts) analyze_bytes(Bytes(data.begin(), data.begin() + long(c)), "trunc");
    }
}

TEST("fuzz: random garbage") {
    std::mt19937 rng(1234);
    for (size_t n : {1, 3, 64, 4096})
        for (std::string prefix : {"", "\x7f" "ELF", "MZ"}) {
            Bytes blob(prefix.begin(), prefix.end());
            for (size_t k = 0; k < n; k++) blob.push_back(uint8_t(rng()));
            analyze_bytes(blob, "garbage");
        }
}
