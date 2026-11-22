#include "session/session.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>

#include "core/errors.hpp"
#include "core/text.hpp"

namespace fs = std::filesystem;

namespace rv {

namespace {

std::string hx(Addr a) { return hex_digits(a); }

std::string strip(const std::string& s) { return trim(s); }

// Python int(text, 16): optional 0x prefix, single underscores between digits.
std::optional<uint64_t> parse_hex(std::string t) {
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    if (t.empty() || t.front() == '_' || t.back() == '_') return std::nullopt;
    uint64_t v = 0;
    int digits = 0;
    char prev = 0;
    for (char c : t) {
        if (c == '_') {
            if (prev == '_') return std::nullopt;
            prev = c;
            continue;
        }
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        if (++digits > 16) return std::nullopt;
        v = (v << 4) | uint64_t(d);
        prev = c;
    }
    return v;
}

std::string home_dir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h && *h ? h : ".";
}

bool writable_dir(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    fs::path probe = dir / ".reversa-write-test";
    {
        std::ofstream f(probe, std::ios::binary);
        if (!f) return false;
    }
    fs::remove(probe, ec);
    return true;
}

}  // namespace

bool valid_name(const std::string& name) {
    // ^[A-Za-z_.$?@][A-Za-z0-9_.$?@:<>~]{0,199}$
    if (name.empty() || name.size() > 200) return false;
    auto first = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || std::strchr("_.$?@", c); };
    auto rest = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || std::strchr("_.$?@:<>~", c); };
    if (static_cast<unsigned char>(name[0]) >= 128 || !first(name[0])) return false;
    for (size_t i = 1; i < name.size(); i++)
        if (static_cast<unsigned char>(name[i]) >= 128 || !rest(name[i])) return false;
    return true;
}

Session::Session(Binary b, LoadOptions options) : binary(std::move(b)), load_options(std::move(options)) {
    events.on_error = [this](const std::string& topic, const std::string& err) {
        runtime_diagnostics.push_back({"error", "Event listener '" + topic + "' failed: " + err, std::nullopt, "address", err});
    };
}

std::unique_ptr<Session> Session::open(const std::string& path, const LoadOptions& options) {
    if (is_project(path)) return open_project(path);
    return std::make_unique<Session>(load_file(path, options), options);
}

std::unique_ptr<Session> Session::open_project(const std::string& path) {
    ProjectData p = load_project(path);
    auto s = std::make_unique<Session>(load_bytes(p.data, p.path, p.load_options), p.load_options);
    s->annotations = std::move(p.annotations);
    s->settings = std::move(p.settings);
    s->project_path = path_to_utf8(fs::absolute(path_from_utf8(path)));
    return s;
}

Session::~Session() {
    // Views registered by plugins are destroyed before the plugins (declaration order), so the
    // plugin libraries are still loaded while their views go away.
    if (plugins) plugins->on_unload();
}

// ------------------------------------------------------------ analysis
Analysis& Session::analyze(Progress progress, const std::atomic<bool>* cancel) {
    if (plugins) plugins->on_load();
    bool hooks = plugins && plugins->has_hook("on_function_analyzed");
    auto a = rv::analyze(binary, std::move(progress), cancel);
    {
        std::lock_guard<std::recursive_mutex> g(lock);
        analysis = std::move(a);
        decomp_cache_.clear();
        symbols_.clear();
        for (auto& s : binary.symbols) symbols_.emplace(s.addr, s.name);
        for (auto& [addr, n] : analysis->auto_names)
            if (!symbols_.count(addr) || n == "main") symbols_[addr] = n;
        name_index_.reset();
    }
    if (hooks)
        for (auto& [addr, fn] : analysis->functions)
            if (fn.kind == "normal") plugins->on_function_analyzed(addr);
    events.publish("analysis_done");
    return *analysis;
}

Analysis& Session::require_analysis() {
    if (!analysis) analyze();
    return *analysis;
}

// ------------------------------------------------------------ names
std::string Session::default_name(Addr addr) const {
    if (analysis) {
        if (analysis->functions.count(addr)) return "sub_" + hx(addr);
        if (auto* st = analysis->string(addr)) {
            std::string slug;
            for (char c : utf8_prefix(st->text, 24)) {
                if (static_cast<unsigned char>(c) < 128 && std::isalnum(static_cast<unsigned char>(c)))
                    slug += c;
                else if (slug.empty() || slug.back() != '_')
                    slug += '_';
            }
            while (!slug.empty() && slug.front() == '_') slug.erase(0, 1);
            while (!slug.empty() && slug.back() == '_') slug.pop_back();
            return slug.empty() ? "str_" + hx(addr) : "s_" + slug + "_" + hx(addr);
        }
        auto* fn = analysis->function_containing(addr);
        if (fn && fn->kind == "normal" && addr != fn->addr) return "loc_" + hx(addr);
    }
    if (binary.is_code(addr)) return "code_" + hx(addr);
    if (binary.is_mapped(addr)) return "data_" + hx(addr);
    return "0x" + hx(addr);
}

std::string Session::name_of(Addr addr) const {
    auto u = annotations.names.find(addr);
    if (u != annotations.names.end() && !u->second.empty()) return u->second;
    auto s = symbols_.find(addr);
    if (s != symbols_.end() && !s->second.empty()) return s->second;
    return default_name(addr);
}

const std::map<std::string, Addr>& Session::names_index() {
    if (!name_index_) {
        std::map<std::string, Addr> idx;
        if (analysis)
            for (auto& [name, addr] : analysis->import_addr) idx.emplace(name, addr);
        for (auto& [addr, n] : symbols_) idx.emplace(n, addr);
        for (auto& [addr, n] : annotations.names) idx[n] = addr;
        name_index_ = std::move(idx);
    }
    return *name_index_;
}

std::optional<Addr> Session::resolve_addr(Addr v) const {
    if (binary.is_mapped(v) || (analysis && analysis->functions.count(v))) return v;
    return std::nullopt;
}

std::optional<Addr> Session::resolve(const std::string& ref) {
    std::lock_guard<std::recursive_mutex> g(lock);
    std::string text = strip(ref);
    const auto& idx = names_index();
    for (const std::string& cand : {text, "_" + text, text.substr(0, text.find('@')), "__imp_" + text}) {
        auto it = idx.find(cand);
        if (it != idx.end()) return it->second;
    }
    static const std::regex DEFAULT_NAME(R"((?:sub|loc|data|str|s_\w*?)_([0-9a-fA-F]+))");
    std::smatch m;
    if (std::regex_match(text, m, DEFAULT_NAME)) text = m[1];  // default names like sub_401196 are not indexed
    auto v = parse_hex(text);
    if (!v) return std::nullopt;
    return resolve_addr(*v);
}

std::optional<std::string> Session::string_text(Addr addr) const {
    if (analysis)
        if (auto* s = analysis->string(addr)) return s->text;
    return std::nullopt;
}

bool Session::is_symbol(int64_t v) const {
    if (!analysis || v < 0x100) return false;
    Addr a = Addr(v);
    if (analysis->string_at.count(a) || analysis->functions.count(a) || annotations.names.count(a)) return true;
    if (v >= 0x1000) {
        auto* s = binary.section_at(a);
        return s && !s->executable;
    }
    return false;
}

// ------------------------------------------------------------ edits (undoable)
void Session::edit(const std::string& description, std::function<void()> redo, std::function<void()> undo) {
    std::lock_guard<std::recursive_mutex> g(lock);
    undo_stack.push({description, std::move(redo), std::move(undo)});
    dirty = true;
}

std::string Session::rename(const std::string& ref, const std::string& new_name) {
    auto addr = resolve(ref);
    if (!addr) throw AnnotationError("Address or name '" + ref + "' not found.");
    return rename(*addr, new_name);
}

std::string Session::rename(Addr addr, const std::string& new_name_in) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!binary.is_mapped(addr)) {
        bool known = false;
        for (auto& [n, a] : names_index()) known = known || a == addr;
        if (!known) throw AnnotationError("Address 0x" + hx(addr) + " is not in any section of the file.");
    }
    std::string new_name = strip(new_name_in);
    if (!new_name.empty() && !valid_name(new_name))
        throw AnnotationError("A name may contain letters, digits and _ . $ @ : (no spaces, at most 200 characters) "
                              "and must not start with a digit.");
    auto other = names_index().find(new_name);
    if (!new_name.empty() && other != names_index().end() && other->second != addr)
        throw AnnotationError("The name '" + new_name + "' is already used by address 0x" + hx(other->second) + ". Choose another name.");
    std::optional<std::string> old_user;
    if (annotations.names.count(addr)) old_user = annotations.names[addr];
    std::string old_display = name_of(addr);
    auto apply = [this, addr, old_display](std::optional<std::string> value) {
        return [this, addr, old_display, value]() {
            if (value && !value->empty())
                annotations.names[addr] = *value;
            else
                annotations.names.erase(addr);
            name_index_.reset();
            std::string now = name_of(addr);
            events.publish("renamed", {{"addr", uint64_t(addr)}, {"old", old_display}, {"new", now}});
            if (plugins) plugins->on_rename(addr, old_display, now);
        };
    };
    edit("Rename 0x" + hx(addr) + " to " + (new_name.empty() ? "(original)" : new_name),
         apply(new_name.empty() ? std::nullopt : std::optional<std::string>(new_name)), apply(old_user));
    return name_of(addr);
}

void Session::rename_var(const std::string& func, const std::string& key, const std::string& new_name) {
    auto faddr = resolve(func);
    if (!faddr) throw AnnotationError("Function '" + func + "' not found.");
    rename_var(*faddr, key, new_name);
}

void Session::rename_var(Addr func_addr, const std::string& key, const std::string& new_name_in) {
    std::optional<Addr> faddr = func_addr;
    std::string new_name = strip(new_name_in);
    if (!new_name.empty() && !valid_name(new_name))
        throw AnnotationError("Invalid variable name (letters, digits, _; must not start with a digit).");
    std::pair<Addr, std::string> k{*faddr, key};
    std::optional<std::string> old;
    if (annotations.var_names.count(k)) old = annotations.var_names[k];
    Addr fa = *faddr;
    auto apply = [this, k, fa, key](std::optional<std::string> value) {
        return [this, k, fa, key, value]() {
            if (value && !value->empty())
                annotations.var_names[k] = *value;
            else
                annotations.var_names.erase(k);
            events.publish("var_renamed", {{"func", uint64_t(fa)}, {"key", key}, {"new", value ? EventValue(*value) : EventValue()}});
        };
    };
    edit("Rename variable " + key, apply(new_name.empty() ? std::nullopt : std::optional<std::string>(new_name)), apply(old));
}

Addr Session::resolve_or_throw(const std::string& ref, const std::string& what) {
    auto addr = resolve(ref);
    if (!addr) throw AnnotationError(what + " '" + ref + "' not found.");
    return *addr;
}

void Session::set_map(std::map<Addr, std::string>& mapping, const std::string& topic, const std::string& label, Addr a,
                      const std::string& value_in) {
    std::lock_guard<std::recursive_mutex> g(lock);
    std::optional<std::string> old;
    if (mapping.count(a)) old = mapping[a];
    std::string v = strip(value_in);
    std::optional<std::string> value = v.empty() ? std::nullopt : std::optional<std::string>(v);
    auto apply = [this, &mapping, topic, a](std::optional<std::string> val) {
        return [this, &mapping, topic, a, val]() {
            if (val)
                mapping[a] = *val;
            else
                mapping.erase(a);
            events.publish(topic, {{"addr", uint64_t(a)}, {"value", val ? EventValue(*val) : EventValue()}});
        };
    };
    edit(label + " 0x" + hx(a), apply(value), apply(old));
}

void Session::set_comment(const std::string& ref, const std::string& text) { set_comment(resolve_or_throw(ref, "Address"), text); }
void Session::set_comment(Addr addr, const std::string& text) { set_map(annotations.comments, "comment", "Comment", addr, text); }
void Session::set_bookmark(const std::string& ref, const std::string& label) { set_bookmark(resolve_or_throw(ref, "Address"), label); }
void Session::set_bookmark(Addr addr, const std::string& label) { set_map(annotations.bookmarks, "bookmark", "Bookmark", addr, label); }

std::optional<std::string> Session::undo() {
    std::lock_guard<std::recursive_mutex> g(lock);
    auto d = undo_stack.undo();
    if (d) dirty = true;
    return d;
}

std::optional<std::string> Session::redo() {
    std::lock_guard<std::recursive_mutex> g(lock);
    auto d = undo_stack.redo();
    if (d) dirty = true;
    return d;
}

// ------------------------------------------------------------ pseudocode
std::vector<Line> Session::decompile(const std::string& func) {
    require_analysis();
    auto faddr = resolve(func);
    if (!faddr || !analysis->functions.count(*faddr)) throw AnnotationError("'" + func + "' is not a function.");
    return decompile(*faddr);
}

std::vector<Line> Session::decompile(Addr faddr) {
    Analysis& a = require_analysis();
    auto it = a.functions.find(faddr);
    if (it == a.functions.end()) throw AnnotationError("'0x" + hx(faddr) + "' is not a function.");
    const Function& fn = it->second;
    auto comment = [](std::string text, std::optional<Addr> addr) {
        return Line{0, {Seg{std::move(text), "comment", std::nullopt, ""}}, addr};
    };
    if (fn.kind != "normal") {
        std::string lib = fn.library.empty() ? "" : " from library " + fn.library;
        return {comment("// " + name_of(faddr) + " is an imported function" + lib + "; its code is not in this file.", faddr)};
    }
    std::lock_guard<std::recursive_mutex> g(lock);
    auto c = decomp_cache_.find(faddr);
    if (c == decomp_cache_.end()) {
        try {
            c = decomp_cache_.emplace(faddr, rv::decompile(a, faddr)).first;
        } catch (const std::exception& e) {  // report instead of crashing the UI
            return {comment(std::string("// Could not create pseudocode: ") + e.what(), faddr)};
        }
    }
    RenderNames names;
    names.name_of = [this](Addr x) { return name_of(x); };
    names.var_override = [this, faddr](const std::string& key) -> std::optional<std::string> {
        auto v = annotations.var_names.find({faddr, key});
        if (v == annotations.var_names.end()) return std::nullopt;
        return v->second;
    };
    names.string_at = [this](Addr x) { return string_text(x); };
    names.is_symbol = [this](int64_t v) { return is_symbol(v); };
    return render(c->second, name_of(faddr), names);
}

// ------------------------------------------------------------ persistence
std::string Session::save(const std::string& path_in) {
    std::string path = !path_in.empty() ? path_in : project_path ? *project_path : default_project_path(binary.path);
    {
        std::lock_guard<std::recursive_mutex> g(lock);
        save_project(path, *binary.data, binary.path, load_options, annotations, settings);
        project_path = path_to_utf8(fs::absolute(path_from_utf8(path)));
        dirty = false;
    }
    events.publish("saved", {{"path", *project_path}});
    return *project_path;
}

// ------------------------------------------------------------ search
Session::Hits Session::search_strings(const std::string& query, bool regex, size_t min_len) {
    Hits items;
    for (auto& st : require_analysis().strings)
        if (utf8_length(st.text) >= min_len) items.emplace_back(st.addr, st.text);
    return search_text(items, query, regex);
}

Session::Hits Session::search_instructions(const std::string& query, bool regex) {
    Analysis& a = require_analysis();
    std::vector<Addr> addrs;
    for (auto& [ad, ins] : a.insns) addrs.push_back(ad);
    std::sort(addrs.begin(), addrs.end());
    Hits items;
    for (Addr ad : addrs) items.emplace_back(ad, a.insns[ad]->text());
    return search_text(items, query, regex);
}

Session::Hits Session::search_pseudocode(const std::string& query, bool regex) {
    Analysis& a = require_analysis();
    Hits out;
    std::regex rx;
    if (regex) {
        try {
            rx = std::regex(query, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error& e) {
            throw ReversaError(std::string("Invalid regular expression: ") + e.what());
        }
    }
    std::string ql = lower(query);
    for (auto& [ad, f] : a.functions) {
        if (f.kind != "normal") continue;
        for (auto& line : split(pseudocode(ad), '\n'))
            if (regex ? std::regex_search(line, rx) : contains(lower(line), ql)) out.emplace_back(ad, trim(line));
    }
    return out;
}

void Session::add_view(const std::string& name, std::function<std::string()> render) {
    for (auto& v : views)
        if (v.first == name) {
            v.second = std::move(render);
            return;
        }
    views.emplace_back(name, std::move(render));
}

std::vector<Diagnostic> Session::diagnostics() const {
    auto d = binary.diagnostics;
    d.insert(d.end(), runtime_diagnostics.begin(), runtime_diagnostics.end());
    return d;
}

std::string default_project_path(const std::string& binary_path) {
    fs::path bp = path_from_utf8(binary_path);
    std::string base = path_to_utf8(bp.filename());
    if (base.empty()) base = "projekt";
    fs::path folder;
    if (!binary_path.empty() && binary_path[0] != '<') folder = fs::absolute(bp).parent_path();
    std::error_code ec;
    fs::path tmp = fs::weakly_canonical(fs::temp_directory_path(ec), ec);
    bool in_upload_dir = false;
    if (!folder.empty()) {
        auto real = path_to_utf8(fs::weakly_canonical(folder, ec));
        in_upload_dir = starts_with(real, path_to_utf8(tmp)) && starts_with(path_to_utf8(folder.filename()), "reversa-");
    }
    if (folder.empty() || !writable_dir(folder) || in_upload_dir) {
        folder = path_from_utf8(home_dir()) / "reversa-projects";
        fs::create_directories(folder, ec);
    }
    return path_to_utf8(folder / path_from_utf8(base + ".rvproj"));
}

}  // namespace rv
