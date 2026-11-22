// CI license gate (standalone, standard C++ only; build: c++ -std=c++20 tools/check_licenses.cpp).
//
// Collects every third-party component the build pulls in (FetchContent_Declare in CMakeLists.txt,
// find_package(Qt6/SQLite3), vcpkg.json dependencies) and checks that each one has a known, allowed
// license and is listed in LICENSE-THIRD-PARTY. No network access.
//
// Usage: check_licenses [REPO_ROOT]     exit code 0 = OK, 1 = problem (the message explains what to fix)
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

struct Known { std::string license, listed_as; };
// Known components: name -> license and the name used in LICENSE-THIRD-PARTY.
const std::map<std::string, Known> KNOWN = {
    {"capstone", {"BSD-3-Clause", "Capstone"}},
    {"sqlite3", {"Public-Domain", "SQLite"}},
    {"qt6", {"LGPL-3.0-only", "Qt"}},
};
// LGPL is allowed only because Qt is linked dynamically and shipped as separate, replaceable DLLs/.so.
const std::set<std::string> ALLOWED = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "ISC", "Public-Domain",
                                       "LGPL-3.0-only"};

std::string root;

std::string read(const std::string& rel) {
    std::ifstream f(root + "/" + rel, std::ios::binary);
    if (!f) { std::fprintf(stderr, "ERROR: cannot read %s\n", rel.c_str()); std::exit(1); }
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::set<std::string> components() {
    std::set<std::string> found;
    std::string cmake = read("CMakeLists.txt");
    std::regex fetch(R"(FetchContent_Declare\(\s*(\w+))");
    for (std::sregex_iterator it(cmake.begin(), cmake.end(), fetch), end; it != end; ++it) found.insert(lower((*it)[1]));
    for (auto [pkg, name] : {std::pair{"Qt6", "qt6"}, {"SQLite3", "sqlite3"}, {"Python", "python"}, {"pybind11", "pybind11"}})
        if (std::regex_search(cmake, std::regex(std::string(R"(find_package\(\s*)") + pkg + R"(\b)"))) found.insert(name);
    // vcpkg.json: "dependencies": ["a", {"name": "b"}]
    std::string vcpkg = read("vcpkg.json");
    std::smatch deps;
    if (std::regex_search(vcpkg, deps, std::regex(R"re("dependencies"\s*:\s*\[([^\]]*)\])re"))) {
        std::string list = deps[1];
        std::regex item(R"re("name"\s*:\s*"([^"]+)"|"([^"]+)")re");
        for (std::sregex_iterator it(list.begin(), list.end(), item), end; it != end; ++it) {
            std::string n = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
            if (n != "name") found.insert(lower(n));
        }
    }
    return found;
}

}  // namespace

int main(int argc, char** argv) {
    root = argc > 1 ? argv[1] : ".";
    std::string third = lower(read("LICENSE-THIRD-PARTY"));
    int problems = 0;
    for (auto& dep : components()) {
        auto it = KNOWN.find(dep);
        Known k = it != KNOWN.end() ? it->second : Known{"UNKNOWN", dep};
        bool ok = ALLOWED.count(k.license) != 0;
        bool listed = third.find(lower(k.listed_as)) != std::string::npos;
        std::printf("%-12s %-16s %s  %s\n", dep.c_str(), k.license.c_str(), ok ? "OK" : "NOT ALLOWED",
                    listed ? "listed" : "MISSING from LICENSE-THIRD-PARTY");
        if (it == KNOWN.end()) {
            std::fprintf(stderr, "ERROR: %s: unknown dependency, add its license to tools/check_licenses.cpp\n", dep.c_str());
            problems++;
        } else if (!ok) {
            std::fprintf(stderr, "ERROR: %s: license '%s' is not allowed\n", dep.c_str(), k.license.c_str());
            problems++;
        }
        if (!listed) {
            std::fprintf(stderr, "ERROR: %s: add '%s' to LICENSE-THIRD-PARTY\n", dep.c_str(), k.listed_as.c_str());
            problems++;
        }
    }
    std::printf("License check: %s\n", problems ? "FAILED" : "PASSED");
    return problems ? 1 : 0;
}
