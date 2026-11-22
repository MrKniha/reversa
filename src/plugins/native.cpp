#include "plugins/native.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>

#include "core/errors.hpp"
#include "core/text.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// An opened program as seen by plugins and scripts.
struct rv_program {
    rv::Session* session = nullptr;
    std::unique_ptr<rv::Session> owned;  // scripts: host->open() owns the session
};

namespace rv::plugins {

namespace {

// ------------------------------------------------------------ shared libraries
class Library {
public:
    explicit Library(const std::string& path) {
#ifdef _WIN32
        h_ = LoadLibraryW(path_from_utf8(path).wstring().c_str());
        if (!h_) error_ = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
#else
        h_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h_) error_ = dlerror();
#endif
    }
    ~Library() {
#ifdef _WIN32
        if (h_) FreeLibrary(static_cast<HMODULE>(h_));
#else
        if (h_) dlclose(h_);
#endif
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    bool ok() const { return h_ != nullptr; }
    const std::string& error() const { return error_; }
    void* symbol(const char* name) const {
#ifdef _WIN32
        return h_ ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h_), name)) : nullptr;
#else
        return h_ ? dlsym(h_, name) : nullptr;
#endif
    }

private:
    void* h_ = nullptr;
    std::string error_;
};

bool is_library(const fs::path& p) {
#ifdef _WIN32
    return p.extension() == ".dll";
#else
    return p.extension() == ".so";
#endif
}

// ------------------------------------------------------------ host function table
thread_local std::string t_error;
thread_local std::string t_ring[8];
thread_local size_t t_next = 0;

const char* keep(std::string s) {  // valid until a few host calls later on this thread
    std::string& slot = t_ring[t_next++ % 8];
    slot = std::move(s);
    return slot.c_str();
}

template <typename F>
int guard(F&& f) {
    try {
        f();
        return 0;
    } catch (const std::exception& e) {
        t_error = e.what();
    } catch (...) {
        t_error = "unknown error";
    }
    return -1;
}

Session& S(rv_program* p) { return *p->session; }

const char* kind_name(const std::string& k) {
    static const char* const KINDS[] = {"call", "jump", "read", "write", "addr", "data"};
    for (const char* x : KINDS)
        if (k == x) return x;
    return "other";
}

const Function* find_function(Session& s, Addr a) {
    auto& fs_ = s.require_analysis().functions;
    auto it = fs_.find(a);
    return it == fs_.end() ? nullptr : &it->second;
}

void fill(const Function& f, rv_function_info* out) {
    out->addr = f.addr;
    out->size = f.size;
    out->insn_count = uint32_t(f.insn_count);
    out->block_count = uint32_t(f.blocks.size());
    out->is_import = f.kind == "normal" ? 0 : 1;
}

size_t copy_out(const std::vector<Addr>& v, rv_addr* out, size_t cap) {
    for (size_t i = 0; out && i < v.size() && i < cap; i++) out[i] = v[i];
    return v.size();
}

rv_host make_host() {
    rv_host h{};
    h.api_version = RV_API_VERSION;
    h.struct_size = sizeof(rv_host);
    h.last_error = [] { return t_error.c_str(); };
    h.log = [](rv_program*, const char* msg) { std::cout << (msg ? msg : "") << "\n" << std::flush; };
    h.open = [](const char* path) -> rv_program* {
        try {
            auto s = Session::open(path ? path : "");
            s->analyze();
            auto* p = new rv_program;
            p->session = s.get();
            p->owned = std::move(s);
            return p;
        } catch (const std::exception& e) {
            t_error = e.what();
            return nullptr;
        }
    };
    h.close = [](rv_program* p) { delete p; };
    h.save = [](rv_program* p, const char* path) {
        return guard([&] { S(p).save(path ? path : ""); });
    };
    h.path = [](rv_program* p) {
        return keep(S(p).project_path && !S(p).project_path->empty() ? *S(p).project_path : S(p).binary.path);
    };
    h.format = [](rv_program* p) { return keep(S(p).binary.format); };
    h.arch = [](rv_program* p) { return keep(S(p).binary.arch); };
    h.entry = [](rv_program* p, rv_addr* out) {
        auto& b = S(p).binary;
        if (!b.entry || !*b.entry) return t_error = "no entry point", -1;
        *out = *b.entry;
        return 0;
    };
    h.main_function = [](rv_program* p, rv_addr* out) {
        auto& a = S(p).require_analysis();
        if (!a.main || !*a.main) return t_error = "main function not found", -1;
        *out = *a.main;
        return 0;
    };
    h.read = [](rv_program* p, rv_addr addr, void* buf, size_t size) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        Bytes b = S(p).binary.read(addr, size);
        if (buf && !b.empty()) std::memcpy(buf, b.data(), std::min(size, b.size()));
        return std::min(size, b.size());
    };
    h.resolve = [](rv_program* p, const char* ref, rv_addr* out) {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto a = S(p).resolve(ref ? ref : "");
        if (!a) return t_error = std::string("Address or name '") + (ref ? ref : "") + "' not found.", -1;
        *out = *a;
        return 0;
    };
    h.name_of = [](rv_program* p, rv_addr a) {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        return keep(S(p).name_of(a));
    };
    h.function_count = [](rv_program* p) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        return S(p).require_analysis().functions.size();
    };
    h.function_at = [](rv_program* p, size_t i, rv_function_info* out) {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto& fs_ = S(p).require_analysis().functions;
        if (i >= fs_.size()) return t_error = "function index out of range", -1;
        fill(std::next(fs_.begin(), long(i))->second, out);
        return 0;
    };
    h.function_info = [](rv_program* p, rv_addr a, rv_function_info* out) {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto* f = find_function(S(p), a);
        if (!f) f = S(p).require_analysis().function_containing(a);
        if (!f) return t_error = "'" + hex(a) + "' is not a function.", -1;
        fill(*f, out);
        return 0;
    };
    h.callers = [](rv_program* p, rv_addr f, rv_addr* out, size_t cap) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        return copy_out(S(p).require_analysis().callers(f), out, cap);
    };
    h.callees = [](rv_program* p, rv_addr f, rv_addr* out, size_t cap) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto* fn = find_function(S(p), f);
        if (!fn) return 0;
        return copy_out(std::vector<Addr>(fn->callees.begin(), fn->callees.end()), out, cap);
    };
    h.xrefs_to = [](rv_program* p, rv_addr a, rv_xref* out, size_t cap) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto xs = S(p).require_analysis().xrefs.to(a);
        for (size_t i = 0; out && i < xs.size() && i < cap; i++) out[i] = {xs[i].frm, xs[i].to, kind_name(xs[i].kind)};
        return xs.size();
    };
    h.pseudocode = [](rv_program* p, rv_addr f) -> const char* {
        std::string text;
        if (guard([&] {
                std::lock_guard<std::recursive_mutex> g(S(p).lock);
                if (!find_function(S(p), f)) throw ReversaError("'" + hex(f) + "' is not a function.");
                text = S(p).pseudocode(f);
            }) != 0)
            return nullptr;
        return keep(std::move(text));
    };
    h.string_count = [](rv_program* p) -> size_t {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        return S(p).require_analysis().strings.size();
    };
    h.string_at = [](rv_program* p, size_t i, rv_addr* addr, const char** text) {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto& ss = S(p).require_analysis().strings;
        if (i >= ss.size()) return t_error = "string index out of range", -1;
        if (addr) *addr = ss[i].addr;
        if (text) *text = keep(ss[i].text);
        return 0;
    };
    h.search_bytes = [](rv_program* p, const char* pattern, rv_addr* out, size_t cap) -> size_t {
        std::vector<Addr> hits;
        if (guard([&] { hits = S(p).search_bytes(pattern ? pattern : ""); }) != 0) return size_t(-1);
        return copy_out(hits, out, cap);
    };
    h.rename = [](rv_program* p, rv_addr a, const char* name) {
        return guard([&] { S(p).rename(a, name ? name : ""); });
    };
    h.set_comment = [](rv_program* p, rv_addr a, const char* text) {
        return guard([&] { S(p).set_comment(a, text ? text : ""); });
    };
    h.set_bookmark = [](rv_program* p, rv_addr a, const char* label) {
        return guard([&] { S(p).set_bookmark(a, label ? label : ""); });
    };
    h.comment = [](rv_program* p, rv_addr a) -> const char* {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto& m = S(p).annotations.comments;
        auto it = m.find(a);
        return it == m.end() ? nullptr : keep(it->second);
    };
    h.bookmark = [](rv_program* p, rv_addr a) -> const char* {
        std::lock_guard<std::recursive_mutex> g(S(p).lock);
        auto& m = S(p).annotations.bookmarks;
        auto it = m.find(a);
        return it == m.end() ? nullptr : keep(it->second);
    };
    h.add_view = [](rv_program* p, const char* name, rv_view_fn render, void* user) {
        if (!render) return;
        S(p).add_view(name ? name : "?", [p, render, user] {
            const char* t = render(p, user);
            return std::string(t ? t : "");
        });
    };
    return h;
}

// ------------------------------------------------------------ plugin host
struct Loaded {
    std::string path, name, version, description, status = "discovered", error;
    std::unique_ptr<Library> lib;
    const rv_plugin* api = nullptr;
    void* state = nullptr;
    int calls = 0;
};

class NativeHost : public PluginHost {
public:
    explicit NativeHost(Session& s) : session_(s), program_(make_program(s)) {}
    ~NativeHost() override {
        for (auto& p : plugins_)
            if (p.api && p.state && p.api->destroy) p.api->destroy(p.state);
        free_program(program_);
    }

    void load_file(const std::string& path) {
        Loaded p;
        p.path = path;
        p.name = path_to_utf8(path_from_utf8(path).stem());
        p.lib = std::make_unique<Library>(path);
        if (!p.lib->ok()) return fail_load(std::move(p), "cannot open the library: " + p.lib->error());
        auto entry = reinterpret_cast<rv_plugin_entry>(p.lib->symbol("reversa_plugin"));
        if (!entry) return fail_load(std::move(p), "the library does not export reversa_plugin()");
        p.api = entry();
        if (!p.api) return fail_load(std::move(p), "reversa_plugin() returned NULL");
        if (p.api->name) p.name = p.api->name;
        if (p.api->version) p.version = p.api->version;
        if (p.api->description) p.description = p.api->description;
        if (p.api->api_version != RV_API_VERSION) {
            p.status = "incompatible";
            p.error = "The plugin needs API " + std::to_string(p.api->api_version) + ", REVERSA provides " +
                      std::to_string(RV_API_VERSION) + ".";
            report(p, "Plugin '" + p.name + "' was not loaded: " + p.error, p.path);
            p.api = nullptr;
            plugins_.push_back(std::move(p));
            return;
        }
        if (p.api->create) p.state = p.api->create(host_table(), program_);
        p.status = "active";
        plugins_.push_back(std::move(p));
    }

    bool has_hook(const std::string& hook) override {
        for (auto& p : plugins_)
            if (p.status == "active" && hook_ptr(p, hook)) return true;
        return false;
    }
    void on_load() override {
        call("on_load", [](Loaded& p) { return p.api->on_load ? p.api->on_load(p.state) : 0; });
    }
    void on_function_analyzed(Addr addr) override {
        call("on_function_analyzed",
             [addr](Loaded& p) { return p.api->on_function_analyzed ? p.api->on_function_analyzed(p.state, addr) : 0; });
    }
    void on_rename(Addr addr, const std::string& old_name, const std::string& new_name) override {
        call("on_rename", [&](Loaded& p) {
            return p.api->on_rename ? p.api->on_rename(p.state, addr, old_name.c_str(), new_name.c_str()) : 0;
        });
    }
    void on_unload() override {
        call("on_unload", [](Loaded& p) { return p.api->on_unload ? p.api->on_unload(p.state) : 0; });
    }

    Json info() const {
        Json out = Json::array();
        for (auto& p : plugins_) {
            Json hooks = Json::array();
            if (p.api)
                for (const char* h : {"on_load", "on_function_analyzed", "on_rename", "on_unload"})
                    if (hook_ptr(p, h)) hooks.push(Json(std::string(h)));
            Json o = Json::object();
            o.set("name", Json(p.name));
            o.set("version", Json(p.version));
            o.set("path", Json(p.path));
            o.set("description", Json(p.description));
            o.set("status", Json(p.status));
            o.set("error", Json(p.error));
            o.set("hooks", hooks);
            o.set("calls", Json(int64_t(p.calls)));
            out.push(o);
        }
        return out;
    }

private:
    Session& session_;
    rv_program* program_;
    std::vector<Loaded> plugins_;

    static bool hook_ptr(const Loaded& p, const std::string& hook) {
        if (!p.api) return false;
        if (hook == "on_load") return p.api->on_load != nullptr;
        if (hook == "on_function_analyzed") return p.api->on_function_analyzed != nullptr;
        if (hook == "on_rename") return p.api->on_rename != nullptr;
        if (hook == "on_unload") return p.api->on_unload != nullptr;
        return false;
    }
    template <typename F>
    void call(const char* hook, F&& f) {
        for (auto& p : plugins_) {
            if (p.status != "active" || !p.api) continue;
            int rc = f(p);
            p.calls++;
            if (rc != 0) {
                p.status = "failed";
                const char* why = p.api->error ? p.api->error(p.state) : nullptr;
                p.error = std::string(hook) + ": " + (why && *why ? why : "error " + std::to_string(rc));
                report(p, "Plugin '" + p.name + "' failed in " + hook + " and was disabled. REVERSA continues.", p.error);
            }
        }
    }
    void fail_load(Loaded p, const std::string& why) {
        p.status = "failed";
        p.error = why;
        report(p, "Could not load plugin '" + p.name + "': " + why, p.path);
        p.api = nullptr;
        plugins_.push_back(std::move(p));
    }
    void report(const Loaded& p, const std::string& message, const std::string& detail) {
        std::lock_guard<std::recursive_mutex> g(session_.lock);
        session_.runtime_diagnostics.push_back({"error", message, std::nullopt, "address", detail});
        std::fprintf(stderr, "%s\n", message.c_str());
        (void)p;
    }
};

std::string home_dir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? h : ".";
}

}  // namespace

const rv_host* host_table() {
    static const rv_host h = make_host();
    return &h;
}

rv_program* make_program(Session& s) {
    auto* p = new rv_program;
    p->session = &s;
    return p;
}

void free_program(rv_program* p) { delete p; }

std::vector<std::string> default_dirs() {
    std::vector<std::string> dirs = {path_to_utf8(path_from_utf8(home_dir()) / ".reversa" / "plugins")};
    if (const char* env = std::getenv("REVERSA_PLUGIN_PATH")) {
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        for (auto& d : split(env, sep))
            if (!d.empty()) dirs.push_back(d);
    }
    return dirs;
}

std::shared_ptr<PluginHost> load(Session& s, const std::vector<std::string>& extra_dirs, bool use_default_dirs) {
    auto host = std::make_shared<NativeHost>(s);
    std::vector<std::string> dirs = use_default_dirs ? default_dirs() : std::vector<std::string>{};
    dirs.insert(dirs.end(), extra_dirs.begin(), extra_dirs.end());
    std::set<fs::path> seen;
    for (auto& d : dirs) {
        std::error_code ec;
        fs::path dir = path_from_utf8(d);
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (e.is_regular_file(ec) && is_library(e.path())) files.push_back(e.path());
            else if (e.is_directory(ec))  // one plugin per folder is fine too: plugins/<name>/<name>.so
                for (auto& f : fs::directory_iterator(e.path(), ec))
                    if (f.is_regular_file(ec) && is_library(f.path())) files.push_back(f.path());
        }
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            fs::path canon = fs::weakly_canonical(f, ec);
            if (!seen.insert(ec ? f : canon).second) continue;
            host->load_file(path_to_utf8(f));
        }
    }
    return host;
}

Json info(const std::shared_ptr<PluginHost>& host) {
    auto* h = dynamic_cast<NativeHost*>(host.get());
    return h ? h->info() : Json::array();
}

int run_script(const std::string& path, const std::vector<std::string>& args) {
    fs::path p = path_from_utf8(path);
    if (p.extension() == ".py")
        throw ReversaError("Python scripts are no longer supported. Build the script as a C++ shared library "
                           "(see docs/scripting-tutorial.md) and run: reversa script my_script" +
#ifdef _WIN32
                           std::string(".dll"));
#else
                           std::string(".so"));
#endif
    std::error_code ec;
    if (!fs::exists(p, ec)) throw ReversaError("Script '" + path + "' does not exist.");
    // A bare file name would make dlopen search the library path instead of the current folder.
    Library lib(path_to_utf8(fs::absolute(p, ec)));
    if (!lib.ok()) throw ReversaError("Cannot load the script '" + path + "'.", lib.error());
    auto entry = reinterpret_cast<rv_script_entry>(lib.symbol("reversa_script"));
    if (!entry) throw ReversaError("'" + path + "' does not export reversa_script(); is it a REVERSA script?");
    std::vector<std::string> argv_s = {path};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<const char*> argv;
    for (auto& a : argv_s) argv.push_back(a.c_str());
    int code = entry(host_table(), int(argv.size()), argv.data());
    std::fflush(stdout);
    return code;
}

}  // namespace rv::plugins
