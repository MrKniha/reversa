#include "report/report.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core/text.hpp"

namespace rv {

namespace {

constexpr size_t MAX_ROWS = 300;

std::string cell(std::string v) {
    std::string out;
    for (char c : v) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

std::string md_table(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
    std::string out = "|";
    for (auto& h : headers) out += " " + h + " |";
    out += "\n|";
    for (size_t i = 0; i < headers.size(); i++) out += "---|";
    for (auto& r : rows) {
        out += "\n|";
        for (auto& c : r) out += " " + cell(c) + " |";
    }
    return out;
}

std::string hx(uint64_t v) { return format_hex(v); }

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

std::string fixed(double v, int digits) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", digits, v);
    return buf;
}

std::string basename_of(const std::string& path) {
    auto p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

}  // namespace

std::string build_markdown(Session& s) {
    Analysis& a = s.require_analysis();
    const Binary& b = s.binary;
    auto name = [&](Addr x) { return s.name_of(x); };
    std::vector<const Function*> funcs, imps;
    for (auto& [ad, f] : a.functions)
        if (f.kind == "normal") funcs.push_back(&f);
    for (Addr ad : a.function_order) {  // ties in the name sort keep discovery order
        auto it = a.functions.find(ad);
        if (it != a.functions.end() && it->second.kind != "normal") imps.push_back(&it->second);
    }
    std::stable_sort(imps.begin(), imps.end(), [&](auto* x, auto* y) { return name(x->addr) < name(y->addr); });
    std::vector<std::string> L;
    auto add = [&](std::string line) { L.push_back(std::move(line)); };

    add("# Analýza: " + basename_of(b.path));
    add("");
    add(std::string("Vygenerované nástrojom REVERSA ") + REVERSA_VERSION + " dňa " + today() +
        ". Pseudokód je dočasný (MVP) – overte ho v listingu.");
    add("");
    add("## Prehľad");
    add("");
    auto meta = b.meta.find("elf_type");
    std::string fmt = b.format + (meta != b.meta.end() && !meta->second.empty() ? " (" + meta->second + ")" : "");
    add(md_table({"Vlastnosť", "Hodnota"},
                 {{"Súbor", b.path},
                  {"SHA-256", b.sha256()},
                  {"Veľkosť", std::to_string(b.data->size()) + " B"},
                  {"Formát", fmt},
                  {"Architektúra", b.arch + ", " + std::to_string(b.bits) + "-bit, " + b.endian + " endian"},
                  {"OS", b.os.empty() ? "?" : b.os},
                  {"Kompilátor (odhad)", b.compiler},
                  {"Vstupný bod (entry)", b.entry && *b.entry ? hx(*b.entry) : "—"},
                  {"Hlavná funkcia", a.main && *a.main ? name(*a.main) + " @ " + hx(*a.main) : "nenájdená"},
                  {"Funkcie / importy / reťazce",
                   std::to_string(funcs.size()) + " / " + std::to_string(imps.size()) + " / " + std::to_string(a.strings.size())},
                  {"Čas analýzy", fixed(a.duration, 2) + " s"}}));
    add("");
    auto diags = s.diagnostics();
    if (!diags.empty()) {
        add("## Upozornenia pri načítaní");
        add("");
        for (auto& d : diags) add("- **" + d.severity + "**: " + d.friendly());
        add("");
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (auto& sec : b.sections) rows.push_back({sec.name, hx(sec.addr), std::to_string(sec.size), sec.perms(), sec.kind()});
        add("## Sekcie");
        add("");
        add(md_table({"Meno", "Adresa", "Veľkosť", "Práva", "Typ"}, rows));
        add("");
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (auto* f : imps) {
            if (rows.size() >= MAX_ROWS) break;
            rows.push_back({name(f->addr), f->library.empty() ? "—" : f->library, std::to_string(a.callers(f->addr).size())});
        }
        add("## Importy");
        add("");
        add(md_table({"Meno", "Knižnica", "Volaní z"}, rows));
        add("");
    }
    if (!b.exports.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (auto& e : b.exports) {
            if (rows.size() >= MAX_ROWS) break;
            rows.push_back({e.name, hx(e.addr)});
        }
        add("## Exporty");
        add("");
        add(md_table({"Meno", "Adresa"}, rows));
        add("");
    }
    {
        std::vector<std::vector<std::string>> rows;
        for (auto* f : funcs) {
            if (rows.size() >= MAX_ROWS) break;
            std::string calls;
            bool first = true;
            for (Addr c : f->callees) {
                calls += (first ? "" : ", ") + name(c);
                first = false;
            }
            rows.push_back({name(f->addr), hx(f->addr), std::to_string(f->size), std::to_string(a.callers(f->addr).size()),
                            utf8_prefix(calls, 120)});
        }
        add("## Funkcie");
        add("");
        add(md_table({"Meno", "Adresa", "Veľkosť", "Volaná z", "Volá"}, rows));
        add("");
    }
    {
        std::vector<const String*> strs;
        for (auto& st : a.strings) strs.push_back(&st);
        std::stable_sort(strs.begin(), strs.end(),
                         [&](auto* x, auto* y) { return a.xrefs.to(x->addr).size() > a.xrefs.to(y->addr).size(); });
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < strs.size() && i < 100; i++)
            rows.push_back({hx(strs[i]->addr), strs[i]->encoding, utf8_prefix(strs[i]->text, 80),
                            std::to_string(a.xrefs.to(strs[i]->addr).size())});
        add("## Reťazce (výber)");
        add("");
        add(md_table({"Adresa", "Kódovanie", "Text", "Odkazy"}, rows));
        add("");
    }
    if (a.main && *a.main) {
        add("## Pseudokód hlavnej funkcie `" + name(*a.main) + "`");
        add("");
        add("```c");
        add(s.pseudocode(*a.main));
        add("```");
        add("");
    }
    const Annotations& ann = s.annotations;
    if (!ann.names.empty() || !ann.comments.empty() || !ann.bookmarks.empty()) {
        add("## Anotácie používateľa");
        add("");
        for (auto& [k, v] : ann.names) add("- " + hx(k) + ": premenované na `" + v + "`");
        for (auto& [k, v] : ann.comments) add("- " + hx(k) + ": komentár „" + v + "“");
        for (auto& [k, v] : ann.bookmarks) add("- " + hx(k) + ": záložka „" + v + "“");
        add("");
    }
    for (auto& [vname, render] : s.views) {
        add("## Pohľad: " + vname);
        add("");
        try {
            add(render());
        } catch (const std::exception& e) {
            add(std::string("(chyba pri vytváraní pohľadu: ") + e.what() + ")");
        }
        add("");
    }
    std::string out;
    for (size_t i = 0; i < L.size(); i++) out += (i ? "\n" : "") + L[i];
    return out;
}

std::string build_html(Session& s) {
    std::string md = build_markdown(s);
    std::string body;
    bool in_code = false, in_table = false;
    for (const std::string& line : split_lines(md)) {
        if (starts_with(line, "```")) {
            body += in_code ? "</pre>" : "<pre>";
            in_code = !in_code;
            continue;
        }
        if (in_code) {
            body += html_escape(line);
            continue;
        }
        if (starts_with(line, "|")) {
            if (starts_with(line, "|---")) continue;
            std::string inner = line;
            while (!inner.empty() && inner.front() == '|') inner.erase(0, 1);
            while (!inner.empty() && inner.back() == '|') inner.pop_back();
            std::vector<std::string> cells;
            size_t pos = 0;
            while (true) {
                size_t nx = inner.find(" | ", pos);
                std::string c = inner.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos);
                std::string t = trim(c);
                std::string unesc;
                for (size_t i = 0; i < t.size(); i++) {
                    if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') { unesc += '|'; i++; }
                    else unesc += t[i];
                }
                cells.push_back(html_escape(unesc));
                if (nx == std::string::npos) break;
                pos = nx + 3;
            }
            if (!in_table) {
                body += "<table><tr>";
                for (auto& c : cells) body += "<th>" + c + "</th>";
                body += "</tr>";
                in_table = true;
            } else {
                body += "<tr>";
                for (auto& c : cells) body += "<td>" + c + "</td>";
                body += "</tr>";
            }
            continue;
        }
        if (in_table) {
            body += "</table>";
            in_table = false;
        }
        if (starts_with(line, "## ")) {
            body += "<h2>" + html_escape(line.substr(3)) + "</h2>";
        } else if (starts_with(line, "# ")) {
            body += "<h1>" + html_escape(line.substr(2)) + "</h1>";
        } else if (starts_with(line, "- ")) {
            std::string t = html_escape(line.substr(2));
            std::string clean;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i] == '*' && i + 1 < t.size() && t[i + 1] == '*') { i++; continue; }
                clean += t[i];
            }
            body += "<li>" + clean + "</li>";
        } else if (!line.empty()) {
            body += "<p>" + html_escape(line) + "</p>";
        }
    }
    if (in_table) body += "</table>";
    const char* style =
        "body{font-family:system-ui,sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#1d2330}"
        "table{border-collapse:collapse;margin:.5rem 0}td,th{border:1px solid #ccd;padding:.25rem .5rem;font-size:.9rem}"
        "th{background:#eef}pre{background:#f4f5f8;padding:1rem;overflow:auto}"
        "@media(prefers-color-scheme:dark){body{background:#14161c;color:#dde}th{background:#252a38}pre{background:#1c1f28}"
        "td,th{border-color:#334}}";
    return std::string("<!doctype html><html lang=sk><meta charset=utf-8><title>REVERSA report</title><style>") + style +
           "</style><body>" + body + "</body></html>";
}

std::string build_report(Session& s, const std::string& fmt) { return fmt == "html" ? build_html(s) : build_markdown(s); }

Json summary(Session& s) {
    Analysis& a = s.require_analysis();
    const Binary& b = s.binary;
    size_t funcs = 0, imps = 0;
    for (auto& [ad, f] : a.functions) (f.kind == "normal" ? funcs : imps)++;
    Json diags = Json::array();
    for (auto& d : s.diagnostics()) {
        Json j = Json::object();
        j.set("severity", d.severity);
        j.set("message", d.friendly());
        diags.push(j);
    }
    Json o = Json::object();
    o.set("file", b.path);
    o.set("format", b.format);
    o.set("arch", b.arch);
    o.set("bits", b.bits);
    o.set("endian", b.endian);
    o.set("compiler", b.compiler);
    o.set("entry", b.entry && *b.entry ? Json(format_hex(*b.entry)) : Json());
    o.set("main", a.main && *a.main ? Json(s.name_of(*a.main)) : Json());
    o.set("functions", uint64_t(funcs));
    o.set("imports", uint64_t(imps));
    o.set("strings", uint64_t(a.strings.size()));
    o.set("xrefs", uint64_t(a.xrefs.size()));
    o.set("seconds", double(int64_t(a.duration * 1000 + 0.5)) / 1000);
    o.set("diagnostics", diags);
    return o;
}

}  // namespace rv
