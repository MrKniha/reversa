#include "core/json.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "core/text.hpp"

namespace rv {

namespace {
const Json NUL;
const Json::Array EMPTY_ARRAY;
const Json::Object EMPTY_OBJECT;

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    int depth = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("JSON: ") + what + " na pozícii " + std::to_string(i));
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) == 0) { i += n; return true; }
        return false;
    }
    uint32_t hex4() {
        if (i + 4 > s.size()) fail("neúplná \\u sekvencia");
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else fail("neplatná \\u sekvencia");
        }
        return v;
    }
    std::string str() {
        if (s[i] != '"') fail("očakávaný reťazec");
        i++;
        std::string out;
        while (true) {
            if (i >= s.size()) fail("neukončený reťazec");
            char c = s[i++];
            if (c == '"') break;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("neukončený reťazec");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("neplatná escape sekvencia");
            }
        }
        return out;
    }
    Json value() {
        if (++depth > 200) fail("príliš hlboké vnorenie");
        ws();
        if (i >= s.size()) fail("neočakávaný koniec");
        Json out;
        char c = s[i];
        if (c == '{') {
            i++;
            out = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') { i++; depth--; return out; }
            while (true) {
                ws();
                if (i >= s.size()) fail("neočakávaný koniec");
                std::string k = str();
                ws();
                if (i >= s.size() || s[i] != ':') fail("očakávaná ':'");
                i++;
                out.set(k, value());
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                fail("očakávaná ',' alebo '}'");
            }
        } else if (c == '[') {
            i++;
            out = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') { i++; depth--; return out; }
            while (true) {
                out.push(value());
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                fail("očakávaná ',' alebo ']'");
            }
        } else if (c == '"') {
            out = Json(str());
        } else if (lit("true")) {
            out = Json(true);
        } else if (lit("false")) {
            out = Json(false);
        } else if (lit("null")) {
            out = Json();
        } else {
            size_t start = i;
            bool flt = false;
            if (s[i] == '-') i++;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' ||
                                    s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
                if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') flt = true;
                i++;
            }
            std::string num = s.substr(start, i - start);
            if (num.empty() || num == "-") fail("neplatná hodnota");
            try {
                out = flt ? Json(std::stod(num)) : Json(int64_t(std::stoll(num)));
            } catch (...) {
                try { out = Json(std::stod(num)); } catch (...) { fail("neplatné číslo"); }
            }
        }
        depth--;
        return out;
    }
};
}  // namespace

std::string json_quote(const std::string& s) { return "\"" + json_escape(s) + "\""; }

int64_t Json::as_int(int64_t def) const {
    if (v_.index() == 2) return std::get<int64_t>(v_);
    if (v_.index() == 3) return int64_t(std::get<double>(v_));
    return def;
}

double Json::as_double(double def) const {
    if (v_.index() == 2) return double(std::get<int64_t>(v_));
    if (v_.index() == 3) return std::get<double>(v_);
    return def;
}

const Json::Array& Json::as_array() const { return is_array() ? std::get<Array>(v_) : EMPTY_ARRAY; }
const Json::Object& Json::as_object() const { return is_object() ? std::get<Object>(v_) : EMPTY_OBJECT; }

const Json& Json::get(const std::string& key) const {
    for (auto& [k, v] : as_object())
        if (k == key) return v;
    return NUL;
}

bool Json::has(const std::string& key) const {
    for (auto& [k, v] : as_object())
        if (k == key) return true;
    return false;
}

Json& Json::set(const std::string& key, Json value) {
    if (!is_object()) v_ = Object{};
    auto& o = std::get<Object>(v_);
    for (auto& [k, v] : o)
        if (k == key) { v = std::move(value); return *this; }
    o.emplace_back(key, std::move(value));
    return *this;
}

Json& Json::push(Json value) {
    if (!is_array()) v_ = Array{};
    std::get<Array>(v_).push_back(std::move(value));
    return *this;
}

size_t Json::size() const {
    if (is_array()) return std::get<Array>(v_).size();
    if (is_object()) return std::get<Object>(v_).size();
    return 0;
}

void Json::dump_to(std::string& out, int indent, int level) const {
    auto nl = [&](int lvl) {
        if (indent < 0) return;
        out += '\n';
        out.append(size_t(indent * lvl), ' ');
    };
    switch (v_.index()) {
        case 0: out += "null"; break;
        case 1: out += std::get<bool>(v_) ? "true" : "false"; break;
        case 2: out += std::to_string(std::get<int64_t>(v_)); break;
        case 3: {
            double d = std::get<double>(v_);
            if (!std::isfinite(d)) { out += "null"; break; }
            char b[32];
            std::snprintf(b, sizeof b, "%.17g", d);
            out += b;
            break;
        }
        case 4: out += json_quote(std::get<std::string>(v_)); break;
        case 5: {
            auto& a = std::get<Array>(v_);
            out += '[';
            for (size_t k = 0; k < a.size(); k++) {
                if (k) out += indent < 0 ? ", " : ",";
                nl(level + 1);
                a[k].dump_to(out, indent, level + 1);
            }
            if (!a.empty()) nl(level);
            out += ']';
            break;
        }
        case 6: {
            auto& o = std::get<Object>(v_);
            out += '{';
            for (size_t k = 0; k < o.size(); k++) {
                if (k) out += indent < 0 ? ", " : ",";
                nl(level + 1);
                out += json_quote(o[k].first) + ": ";
                o[k].second.dump_to(out, indent, level + 1);
            }
            if (!o.empty()) nl(level);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

Json Json::parse(const std::string& text) {
    Parser p{text};
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("nadbytočné znaky");
    return v;
}

}  // namespace rv
