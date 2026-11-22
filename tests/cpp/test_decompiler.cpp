#include <algorithm>
// Decompiler: IR helpers, structure on known functions, golden pseudocode for every sample.
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "core/text.hpp"
#include "decompiler/internal.hpp"
#include "session/session.hpp"
#include "test.hpp"

using namespace rv;
using namespace rv::ir;

static std::string pseudo(const std::string& sample, const std::string& func) {
    auto s = Session::open(rvtest::sample(sample));
    s->require_analysis();
    auto a = s->resolve(func);
    if (!a) a = s->resolve("_" + func);
    return a ? s->pseudocode(*a) : "";
}

TEST("ir helpers") {
    E x = Var("r:rax");
    CHECK(eq(add(x, Const(0)), x));
    CHECK(eq(add(Const(0), x), x));
    CHECK(eq(add(Const(2), Const(3)), Const(5)));
    CHECK(eq(add(x, Const(-4)), Bin("-", x, Const(4))));
    CHECK(eq(add(Bin("+", x, Const(1)), Const(2)), Bin("+", x, Const(3))));
    CHECK(eq(binop("*", x, Const(1)), x));
    CHECK(eq(binop("&", Bin("==", x, Const(0)), Const(0xFF)), Bin("==", x, Const(0))));
    CHECK(eq(negate(Bin("<", x, Const(1))), Bin(">=", x, Const(1))));
    CHECK(eq(negate(Un("!", x)), x));
    CHECK(negate(Bin("&&", Bin("==", x, Const(1)), Bin("<", x, Const(2))))->key == "||");
    CHECK(eq(substitute(Bin("+", x, x), "r:rax", Const(2)), Const(4)));
    std::vector<const Expr*> nodes;
    walk(Call(x, {Const(1)}), nodes);
    CHECK(nodes.back()->k == K::Const && nodes.back()->value == 1);
    CHECK(!eq(Var("r:rax", 4), Var("r:rax", 8)));
}

TEST("conditions from flags") {
    using rv::dec::cond_from_flags;
    using rv::dec::Flags;
    E a = Var("s:-4"), b = Const(2);
    CHECK(eq(cond_from_flags(Flags{"cmp", a, b}, "le"), Bin("<=", a, b)));
    CHECK(eq(cond_from_flags(Flags{"cmp", a, b}, "b"), Bin("u<", a, b)));
    CHECK(eq(cond_from_flags(Flags{"test", a, a}, "e"), Bin("==", a, Const(0))));
    CHECK(eq(cond_from_flags(Flags{"test", a, b}, "ne"), Bin("!=", Bin("&", a, b), Const(0))));
    CHECK(eq(cond_from_flags(Flags{"res", a, nullptr}, "s"), Bin("<", a, Const(0))));
    CHECK(cond_from_flags(std::nullopt, "e")->key == "?");
    CHECK(cond_from_flags(Flags{"cmp", a, b}, "weird")->key == "?");
}

TEST("python set order matches CPython") {
    // Reference values printed by CPython 3.13 (list(set([...])))
    CHECK((rv::dec::py_set_order_ints({4, 8}) == std::vector<Addr>{8, 4}));
    CHECK((rv::dec::py_set_order_ints({1, 2, 4, 8}) == std::vector<Addr>{8, 1, 2, 4}));
    CHECK((rv::dec::py_set_order_ints({16, 8}) == std::vector<Addr>{16, 8}));
    CHECK((rv::dec::py_set_order_ints({0x401200, 0x401180, 0x4011f0, 0x401300, 0x401123}) ==
           std::vector<Addr>{0x401200, 0x401300, 0x401180, 0x401123, 0x4011f0}));  // after a table resize
}

TEST("sum_to is a while loop on every architecture") {
    for (auto name : {"hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe"}) {
        auto c = pseudo(name, "sum_to");
        CHECK(std::regex_search(c, std::regex(R"(while \(\w+ <= \w+\))")));
        CHECK(c.find("+= ") != std::string::npos && c.find("++;") != std::string::npos);
        CHECK(c.find("return ") != std::string::npos && c.find("goto") == std::string::npos);
    }
}

TEST("main calls with arguments") {
    for (auto name : {"hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe"}) {
        auto c = pseudo(name, "main");
        CHECK(c.find("puts(\"Done.\")") != std::string::npos);
        CHECK(std::regex_search(c, std::regex(R"re(printf\("sum_to\(%d\) = %d\\n", \w+, _?sum_to\(\w+\)\))re")));
        CHECK(c.find("return 0;") != std::string::npos);
    }
}

TEST("void function, params and global array") {
    auto c = pseudo("hello_x64.elf", "greet");
    auto lines = split(c, '\n');
    CHECK(lines.size() > 1 && starts_with(lines[1], "void greet(int32_t a1)"));
    CHECK(c.find("greetings[") != std::string::npos);
}

TEST("golden pseudocode for every sample") {
    namespace fs = std::filesystem;
    std::string dir = std::string(REVERSA_SOURCE_DIR) + "/tests/cpp/golden";
    int checked = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        std::string file = entry.path().filename().string();
        std::string sample = file.substr(0, file.size() - std::string(".pseudo.txt").size());
        std::ifstream f(entry.path(), std::ios::binary);
        std::stringstream want;
        want << f.rdbuf();
        auto s = Session::open(rvtest::sample(sample));
        auto& a = s->require_analysis();
        std::string got;
        for (auto& [ad, fn] : a.functions) {
            if (fn.kind != "normal") continue;
            got += "== " + s->name_of(ad) + " " + hex_digits(ad) + "\n" + s->pseudocode(ad) + "\n";
        }
        std::string exp = want.str();
        exp.erase(std::remove(exp.begin(), exp.end(), '\r'), exp.end());  // tolerate CRLF checkouts
        if (got != exp) {
            size_t i = 0;
            while (i < got.size() && i < exp.size() && got[i] == exp[i]) i++;
            size_t line = size_t(std::count(got.begin(), got.begin() + long(i), '\n')) + 1;
            std::printf("  golden mismatch: %s line %zu (regenerate with rvdump --pseudo)\n    got:  %s\n    want: %s\n",
                        file.c_str(), line, got.substr(i - std::min<size_t>(i, 30), 80).c_str(),
                        exp.substr(i - std::min<size_t>(i, 30), 80).c_str());
        }
        CHECK(got == exp);
        checked++;
    }
    CHECK(checked >= 10);
}
