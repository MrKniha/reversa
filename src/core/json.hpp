// Minimal JSON value with parser and writer (project metadata, reports, CLI output).
// Why own code: the needs are tiny (a few small documents) and it keeps the dependency list short.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace rv {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;  // keeps insertion order

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : v_(b) {}
    Json(int v) : v_(int64_t(v)) {}
    Json(int64_t v) : v_(v) {}
    Json(uint64_t v) : v_(int64_t(v)) {}
    Json(double v) : v_(v) {}
    Json(const char* s) : v_(std::string(s)) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(Array a) : v_(std::move(a)) {}
    Json(Object o) : v_(std::move(o)) {}

    static Json object() { return Json(Object{}); }
    static Json array() { return Json(Array{}); }

    bool is_null() const { return v_.index() == 0; }
    bool is_bool() const { return v_.index() == 1; }
    bool is_int() const { return v_.index() == 2; }
    bool is_number() const { return v_.index() == 2 || v_.index() == 3; }
    bool is_string() const { return v_.index() == 4; }
    bool is_array() const { return v_.index() == 5; }
    bool is_object() const { return v_.index() == 6; }

    bool as_bool(bool def = false) const { return is_bool() ? std::get<bool>(v_) : def; }
    int64_t as_int(int64_t def = 0) const;
    double as_double(double def = 0) const;
    std::string as_string(const std::string& def = "") const { return is_string() ? std::get<std::string>(v_) : def; }
    const Array& as_array() const;
    const Object& as_object() const;

    // object access; get() returns null for missing keys
    const Json& get(const std::string& key) const;
    bool has(const std::string& key) const;
    Json& set(const std::string& key, Json value);
    Json& push(Json value);  // array append
    size_t size() const;

    std::string dump(int indent = -1) const;  // -1 = compact
    static Json parse(const std::string& text);  // throws std::runtime_error

private:
    std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object> v_;
    void dump_to(std::string& out, int indent, int level) const;
};

std::string json_quote(const std::string& s);

}  // namespace rv
