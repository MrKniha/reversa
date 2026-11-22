#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace rvtest {
struct Case { std::string name; std::function<void()> fn; };
std::vector<Case>& registry();
extern int failures;
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
inline std::string sample(const std::string& name) { return std::string(REVERSA_SOURCE_DIR) + "/samples/bin/" + name; }
}  // namespace rvtest

#define RV_CAT2(a, b) a##b
#define RV_CAT(a, b) RV_CAT2(a, b)
#define TEST(name) \
    static void RV_CAT(test_, __LINE__)(); \
    static rvtest::Reg RV_CAT(reg_, __LINE__)(name, RV_CAT(test_, __LINE__)); \
    static void RV_CAT(test_, __LINE__)()
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); rvtest::failures++; } } while (0)
#define CHECK_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::printf("  %s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, #a, #b); rvtest::failures++; } } while (0)
