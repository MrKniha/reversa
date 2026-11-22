#include <algorithm>
#include <regex>

#include "analysis/analysis.hpp"
#include "core/errors.hpp"
#include "core/text.hpp"

namespace rv {

std::string Xref::label() const {
    static const std::map<std::string, std::string> L = {
        {"call", "called from"}, {"jump", "jump from"}, {"read", "read from"}, {"write", "written from"},
        {"addr", "address used in"}, {"data", "pointer in data"}};
    auto it = L.find(kind);
    return it != L.end() ? it->second : kind;
}

void XrefDB::add(Addr frm, Addr to, const std::string& kind) {
    Xref x{frm, to, kind};
    if (to_[to].insert(x).second) total_++;
    from_[frm].insert(x);
}

std::vector<Xref> XrefDB::to(Addr addr, const std::vector<std::string>& kinds) const {
    std::vector<Xref> out;
    auto it = to_.find(addr);
    if (it == to_.end()) return out;
    for (auto& x : it->second)
        if (kinds.empty() || std::find(kinds.begin(), kinds.end(), x.kind) != kinds.end()) out.push_back(x);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return std::tie(a.frm, a.kind) < std::tie(b.frm, b.kind); });
    return out;
}

std::vector<Xref> XrefDB::frm(Addr addr) const {
    std::vector<Xref> out;
    auto it = from_.find(addr);
    if (it == from_.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return std::tie(a.to, a.kind) < std::tie(b.to, b.kind); });
    return out;
}

size_t XrefDB::count_to(Addr addr) const {
    auto it = to_.find(addr);
    return it == to_.end() ? 0 : it->second.size();
}

// ---------------------------------------------------------------- strings (like `strings`, address aware)
constexpr size_t MAX_STRINGS = 200000;

static bool printable(uint8_t c) { return (c >= 0x20 && c <= 0x7e) || c == '\t' || c == '\r' || c == '\n'; }

std::vector<String> find_strings(Binary& b, size_t min_len, bool include_code) {
    std::vector<String> out;
    const Bytes& d = b.bytes();
    bool limit = false;
    for (auto& sec : b.sections) {
        if (sec.file_size == 0 || starts_with(sec.name, ".debug") || (sec.executable && !include_code && b.format != "RAW"))
            continue;
        if (sec.offset >= d.size()) continue;
        size_t n = std::min<uint64_t>(sec.file_size, d.size() - sec.offset);
        const uint8_t* blob = d.data() + sec.offset;
        std::vector<bool> taken(n, false);
        // UTF-16LE: runs of (printable, 0x00) pairs, non-overlapping left to right
        for (size_t i = 0; i + 1 < n;) {
            size_t j = i;
            while (j + 1 < n && printable(blob[j]) && blob[j + 1] == 0) j += 2;
            size_t chars = (j - i) / 2;
            if (chars >= min_len) {
                std::string t;
                for (size_t k = i; k < j; k += 2) t += char(blob[k]);
                out.push_back({sec.addr + i, t, "utf-16le", j - i});
                for (size_t k = i; k < j; k++) taken[k] = true;
                i = j;
            } else {
                i++;
            }
        }
        for (size_t i = 0; i < n;) {
            size_t j = i;
            while (j < n && printable(blob[j])) j++;
            if (j - i >= min_len) {
                if (!taken[i]) {
                    out.push_back({sec.addr + i, std::string(reinterpret_cast<const char*>(blob + i), j - i), "ascii", j - i});
                    if (out.size() >= MAX_STRINGS) break;
                }
                i = j;
            } else {
                i = j + 1;
            }
        }
        if (out.size() >= MAX_STRINGS) { limit = true; break; }
    }
    if (limit)
        b.warn("More than 200000 strings found; showing the first 200000.", std::nullopt, "", "info");
    std::stable_sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.addr < y.addr; });
    return out;
}

// ---------------------------------------------------------------- search
static std::vector<int> parse_hex_pattern(const std::string& pattern) {
    std::string p = pattern;
    std::replace(p.begin(), p.end(), ',', ' ');
    std::vector<std::string> toks;
    for (auto& t : split(p, ' '))
        if (!trim(t).empty()) toks.push_back(trim(t));
    if (toks.size() == 1 && toks[0].size() > 2) {
        std::string t = toks[0];
        toks.clear();
        for (size_t i = 0; i < t.size(); i += 2) toks.push_back(t.substr(i, 2));
    }
    std::vector<int> out;
    for (auto& t : toks) {
        if (t == "??" || t == "?") { out.push_back(-1); continue; }
        size_t used = 0;
        int v = -2;
        try { v = std::stoi(t, &used, 16); } catch (...) {}
        if (v < 0 || v > 255 || used != t.size())
            throw ReversaError("'" + t + "' is not a hexadecimal byte. Example pattern: 48 8b ?? 05");
        out.push_back(v);
    }
    if (out.empty()) throw ReversaError("Empty byte pattern.");
    return out;
}

std::vector<Addr> search_bytes(const Binary& b, const std::string& pattern) {
    auto pat = parse_hex_pattern(pattern);
    std::vector<Addr> hits;
    const Bytes& d = b.bytes();
    for (auto& sec : b.sections) {
        if (sec.offset >= d.size()) continue;
        size_t n = std::min<uint64_t>(sec.file_size, d.size() - sec.offset);
        const uint8_t* blob = d.data() + sec.offset;
        for (size_t i = 0; i + pat.size() <= n;) {
            bool ok = true;
            for (size_t k = 0; k < pat.size(); k++)
                if (pat[k] >= 0 && blob[i + k] != pat[k]) { ok = false; break; }
            if (ok) {
                hits.push_back(sec.addr + i);
                if (hits.size() >= MAX_HITS) return hits;
                i += pat.size();
            } else {
                i++;
            }
        }
    }
    return hits;
}

std::vector<std::pair<Addr, std::string>> search_text(const std::vector<std::pair<Addr, std::string>>& items,
                                                      const std::string& q, bool regex) {
    std::vector<std::pair<Addr, std::string>> out;
    std::regex rx;
    std::string ql = lower(q);
    if (regex) {
        try { rx = std::regex(q, std::regex::ECMAScript | std::regex::icase); }
        catch (const std::regex_error& e) { throw ReversaError(std::string("Invalid regular expression: ") + e.what()); }
    }
    for (auto& [addr, text] : items) {
        bool m = regex ? std::regex_search(text, rx) : contains(lower(text), ql);
        if (m) {
            out.push_back({addr, text});
            if (out.size() >= MAX_HITS) break;
        }
    }
    return out;
}

}  // namespace rv
