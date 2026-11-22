// Analysis facts compared with the golden files samples/expected/<name>.json.
// Used by the tests (tests/cpp/test_golden.cpp) and by `rvdump --expected` to regenerate them.
#pragma once

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

#include "core/json.hpp"
#include "session/session.hpp"

namespace rv::facts {

inline std::string strip_underscores(std::string s) {
    s.erase(0, s.find_first_not_of('_'));
    return s;
}

inline Json sorted_array(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    Json out = Json::array();
    for (auto& s : v) out.push(Json(s));
    return out;
}

inline Json facts(const std::string& path) {
    auto s = Session::open(path);
    auto& a = s->require_analysis();
    std::string base = std::filesystem::path(path).filename().string();
    base = base.substr(0, base.find('_'));
    const std::vector<std::string> user_funcs = base == "hello"
        ? std::vector<std::string>{"main", "sum_to", "classify", "greet"}
        : std::vector<std::string>{"main", "check_password", "check_user"};
    const std::set<std::string> known_imports = {"printf", "puts", "strcmp", "strcpy", "strcat", "strlen", "__libc_start_main"};
    const std::vector<std::string> known_strings = {"Done.", "Usage: login <user> <password>", "admin", "student",
                                                    "Access granted. Flag: CTF{xrefs_are_your_friend}", "sum_to(%d) = %d\n"};

    std::set<std::string> names;
    std::vector<std::string> users, imports, strings, strcmp_callers, main_calls;
    for (auto& [addr, f] : a.functions) {
        std::string n = strip_underscores(s->name_of(addr));
        names.insert(n);
        if (f.kind != "normal" && known_imports.count(n)) imports.push_back(n);
    }
    for (auto& n : user_funcs)
        if (names.count(n)) users.push_back(n);
    for (auto& t : known_strings)
        if (std::any_of(a.strings.begin(), a.strings.end(), [&](auto& x) { return x.text == t; })) strings.push_back(t);
    if (auto strcmp = s->resolve("strcmp"))
        for (auto c : a.callers(*strcmp)) strcmp_callers.push_back(strip_underscores(s->name_of(c)));
    if (a.main)
        for (auto c : a.functions.at(*a.main).callees) main_calls.push_back(strip_underscores(s->name_of(c)));

    Json o = Json::object();
    o.set("format", Json(s->binary.format));
    o.set("arch", Json(s->binary.arch));
    o.set("main_found", Json(a.main.has_value()));
    o.set("user_functions_present", sorted_array(users));
    o.set("imports_present", sorted_array(imports));
    o.set("strings_present", sorted_array(strings));
    o.set("strcmp_callers", sorted_array(strcmp_callers));
    o.set("main_calls", sorted_array(main_calls));
    return o;
}

}  // namespace rv::facts
