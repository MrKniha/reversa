// GUI translations check (standalone, standard C++ only; build: c++ -std=c++20 tools/i18n_check.cpp).
//
// Translatable strings in src/gui are the literals passed to tr(...), T(...) and QT_TR_NOOP(...).
// The source language is English; translations live in src/gui/i18n/<lang>.json as {"source": "translation"}.
//
//   i18n_check [REPO_ROOT]           CI: missing/stale keys, empty values, different %N placeholders -> exit 1
//   i18n_check REPO_ROOT missing     prints the missing keys per language as JSON (for translators)
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> LANGS = {"sk", "de", "fr", "zh"};

std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

char unescape(char c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        default: return c;  // \" \\ \' and anything else: the character itself
    }
}

// Adjacent C++ string literals starting at src[pos] == '"' -> their text.
std::string cpp_literals(const std::string& src, size_t pos) {
    std::string out;
    while (pos < src.size() && src[pos] == '"') {
        for (pos++; pos < src.size() && src[pos] != '"'; pos++) out += src[pos] == '\\' ? unescape(src[++pos]) : src[pos];
        for (pos++; pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])); pos++) {}
    }
    return out;
}

// source text -> first "file:line" where it appears
std::map<std::string, std::string> extract(const fs::path& gui) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(gui))
        if (e.path().extension() == ".cpp" || e.path().extension() == ".hpp") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::map<std::string, std::string> found;
    std::regex call(R"(\b(?:tr|T|QT_TR_NOOP)\(\s*")");
    for (auto& f : files) {
        std::string src = read(f);
        for (std::sregex_iterator it(src.begin(), src.end(), call), end; it != end; ++it) {
            size_t quote = size_t(it->position() + it->length() - 1);
            auto line = std::count(src.begin(), src.begin() + it->position(), '\n') + 1;
            found.emplace(cpp_literals(src, quote), "src/gui/" + f.filename().string() + ":" + std::to_string(line));
        }
    }
    return found;
}

void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xc0 | cp >> 6); out += char(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { out += char(0xe0 | cp >> 12); out += char(0x80 | (cp >> 6 & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
    else { out += char(0xf0 | cp >> 18); out += char(0x80 | (cp >> 12 & 0x3f)); out += char(0x80 | (cp >> 6 & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
}

// A flat JSON object of strings: {"key": "value", ...}
std::vector<std::pair<std::string, std::string>> load_json(const fs::path& p) {
    std::string s = read(p);
    size_t i = 0;
    auto ws = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; };
    auto expect = [&](char c) {
        ws();
        if (i >= s.size() || s[i] != c) throw std::runtime_error(p.string() + ": expected '" + c + "' at byte " + std::to_string(i));
        i++;
    };
    auto str = [&] {
        expect('"');
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] != '\\') { out += s[i++]; continue; }
            char e = s[i + 1];
            i += 2;
            if (e == 'u') {
                unsigned cp = std::stoul(s.substr(i, 4), nullptr, 16);
                i += 4;
                if (cp >= 0xd800 && cp < 0xdc00 && s.compare(i, 2, "\\u") == 0) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (std::stoul(s.substr(i + 2, 4), nullptr, 16) - 0xdc00);
                    i += 6;
                }
                put_utf8(out, cp);
            } else {
                out += e == 'b' ? '\b' : e == 'f' ? '\f' : e == '/' ? '/' : unescape(e);
            }
        }
        i++;
        return out;
    };
    std::vector<std::pair<std::string, std::string>> out;
    expect('{');
    ws();
    if (s[i] == '}') return out;
    for (;;) {
        std::string k = str();
        expect(':');
        out.emplace_back(k, str());
        ws();
        if (s[i] == ',') { i++; continue; }
        expect('}');
        return out;
    }
}

std::vector<std::string> placeholders(const std::string& s) {
    std::vector<std::string> out;
    std::regex rx("%[0-9]");
    for (std::sregex_iterator it(s.begin(), s.end(), rx), end; it != end; ++it) out.push_back(it->str());
    std::sort(out.begin(), out.end());
    return out;
}

std::string short_quote(const std::string& s) {
    std::string out;
    size_t n = std::min<size_t>(s.size(), 70);
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80) n--;  // whole UTF-8 characters
    for (char c : s.substr(0, n)) out += c == '\n' ? std::string("\\n") : std::string(1, c);
    return "'" + out + "'";
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        fs::path root = argc > 1 ? argv[1] : ".";
        fs::path gui = root / "src" / "gui";
        auto keys = extract(gui);
        if (argc > 2 && std::string(argv[2]) == "missing") {
            std::printf("{");
            for (size_t l = 0; l < LANGS.size(); l++) {
                std::map<std::string, std::string> tr;
                for (auto& [k, v] : load_json(gui / "i18n" / (LANGS[l] + ".json"))) tr[k] = v;
                std::printf("%s\n %s: [", l ? "," : "", json_string(LANGS[l]).c_str());
                bool first = true;
                for (auto& [k, where] : keys)
                    if (!tr.count(k)) { std::printf("%s\n  %s", first ? "" : ",", json_string(k).c_str()); first = false; }
                std::printf("]");
            }
            std::printf("\n}\n");
            return 0;
        }
        int errors = 0;
        auto error = [&](const std::string& msg) { std::printf("%s\n", msg.c_str()); errors++; };
        for (auto& lang : LANGS) {
            std::map<std::string, std::string> tr;
            for (auto& [k, v] : load_json(gui / "i18n" / (lang + ".json")))
                if (!tr.emplace(k, v).second) error(lang + ": duplicate key: " + short_quote(k));
            for (auto& [k, where] : keys) {
                auto it = tr.find(k);
                if (it == tr.end()) error(lang + ": missing translation (" + where + "): " + short_quote(k));
                else if (it->second.find_first_not_of(" \t\n") == std::string::npos)
                    error(lang + ": empty translation (" + where + "): " + short_quote(k));
                else if (placeholders(it->second) != placeholders(k))
                    error(lang + ": different %N placeholders (" + where + "): " + short_quote(k));
            }
            for (auto& [k, v] : tr)
                if (!keys.count(k)) error(lang + ": unused key: " + short_quote(k));
        }
        std::printf("%zu texts, %zu languages: %s\n", keys.size(), LANGS.size(),
                    errors ? (std::to_string(errors) + " errors").c_str() : "OK");
        return errors ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
}
