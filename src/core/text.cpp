#include "core/text.hpp"

#include <cctype>
#include <cstdio>

namespace rv {

static int utf8_len(const unsigned char* p, size_t n) {
    unsigned char c = p[0];
    int len;
    uint32_t cp;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return 0;
    if (n < static_cast<size_t>(len)) return 0;
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return len;
}

bool is_valid_utf8(std::string_view s) {
    auto p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0;
    while (i < s.size()) {
        int l = utf8_len(p + i, s.size() - i);
        if (!l) return false;
        i += l;
    }
    return true;
}

std::string latin1_to_utf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) out += static_cast<char>(c);
        else { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string utf8_lossy(std::string_view s) {
    std::string out;
    auto p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0;
    while (i < s.size()) {
        int l = utf8_len(p + i, s.size() - i);
        if (!l) { out += "\xEF\xBF\xBD"; i++; continue; }
        out.append(s.substr(i, l));
        i += l;
    }
    return out;
}

std::string decode_text(std::string_view s) { return is_valid_utf8(s) ? std::string(s) : latin1_to_utf8(s); }

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }
bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool ends_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == sep) { out.emplace_back(s.substr(start, i - start)); start = i + 1; }
    }
    return out;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return std::string(s.substr(a, b - a));
}

std::string format_hex(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string json_escape(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    return out;
}

std::string html_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::filesystem::path path_from_utf8(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string path_to_utf8(const std::filesystem::path& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace rv

namespace rv {
std::string utf8_prefix(const std::string& s, size_t chars) {
    size_t i = 0, n = 0;
    while (i < s.size() && n < chars) {
        i++;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) i++;
        n++;
    }
    return s.substr(0, i);
}
}  // namespace rv

namespace rv {
std::string hex_digits(uint64_t v) { return format_hex(v).substr(2); }
}  // namespace rv
