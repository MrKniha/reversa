// Minimal self-contained test runner (no third-party framework needed).
#include "test.hpp"

#include <cstdio>

namespace rvtest {
std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
int failures = 0;
}  // namespace rvtest

int main(int argc, char** argv) {
    int ran = 0, failed_cases = 0;
    for (auto& c : rvtest::registry()) {
        if (argc > 1 && c.name.find(argv[1]) == std::string::npos) continue;
        int before = rvtest::failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            std::printf("  EXCEPTION in %s: %s\n", c.name.c_str(), e.what());
            rvtest::failures++;
        }
        ran++;
        if (rvtest::failures != before) {
            failed_cases++;
            std::printf("FAIL %s\n", c.name.c_str());
        }
    }
    std::printf("%d tests, %d failed\n", ran, failed_cases);
    return failed_cases ? 1 : 0;
}
