// C++17 helpers on top of reversa/plugin.h: a Program class, a Plugin base class and two macros.
// Header-only; a plugin or script built with it still talks to REVERSA only through the C ABI.
//
//   struct Hello : reversa::Plugin {
//       void on_function_analyzed(const reversa::Function& f) override { ... }
//   };
//   REVERSA_PLUGIN(Hello, "hello-plugin", "1.0.0", "Counts functions")
//
//   int run(const rv_host* host, const std::vector<std::string>& args) {
//       auto prog = reversa::Program::open(host, args.at(1));
//       for (auto& f : prog.callers_of("strcmp")) std::printf("%s\n", f.name.c_str());
//       return 0;
//   }
//   REVERSA_SCRIPT(run)
#ifndef REVERSA_SDK_HPP
#define REVERSA_SDK_HPP

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "reversa/plugin.h"

namespace reversa {

using Addr = rv_addr;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Function {
    Addr addr = 0;
    std::string name;
    uint64_t size = 0;
    uint32_t insn_count = 0, block_count = 0;
    bool is_import = false;
};

struct Xref {
    Addr from = 0, to = 0;
    std::string kind;
};

struct StringRef {
    Addr addr = 0;
    std::string text;
};

inline std::string hex(Addr a) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(a));
    return buf;
}

class Program {
public:
    Program() = default;
    Program(const rv_host* host, rv_program* program) : h_(host), state_(std::make_shared<State>()) { state_->p = program; }

    // Scripts: open and analyse a file; the program is closed when the last copy goes away.
    static Program open(const rv_host* host, const std::string& path) {
        rv_program* p = host->open(path.c_str());
        if (!p) throw Error(host->last_error());
        Program prog(host, p);
        prog.state_->host = host;
        prog.state_->owned = true;
        return prog;
    }

    bool valid() const { return h_ && state_ && state_->p; }
    const rv_host* host() const { return h_; }
    rv_program* handle() const { return state_ ? state_->p : nullptr; }

    std::string path() const { return h_->path(p()); }
    std::string format() const { return h_->format(p()); }
    std::string arch() const { return h_->arch(p()); }
    std::optional<Addr> entry() const { return opt(h_->entry); }
    std::optional<Function> main() const {
        auto a = opt(h_->main_function);
        return a ? std::optional<Function>(function(*a)) : std::nullopt;
    }

    std::optional<Addr> resolve(const std::string& ref) const {
        Addr a = 0;
        return h_->resolve(p(), ref.c_str(), &a) == 0 ? std::optional<Addr>(a) : std::nullopt;
    }
    Addr addr(const std::string& ref) const {
        Addr a = 0;
        check(h_->resolve(p(), ref.c_str(), &a));
        return a;
    }
    std::string name_of(Addr a) const { return h_->name_of(p(), a); }

    std::vector<Function> functions(bool include_imports = false) const {
        std::vector<Function> out;
        size_t n = h_->function_count(p());
        for (size_t i = 0; i < n; i++) {
            rv_function_info fi{};
            if (h_->function_at(p(), i, &fi) == 0 && (include_imports || !fi.is_import)) out.push_back(make(fi));
        }
        return out;
    }
    Function function(Addr a) const {
        rv_function_info fi{};
        check(h_->function_info(p(), a, &fi));
        return make(fi);
    }
    Function function(const std::string& ref) const { return function(addr(ref)); }
    std::vector<Function> callers_of(Addr f) const { return funcs(list(h_->callers, f)); }
    std::vector<Function> callers_of(const std::string& ref) const {
        auto a = resolve(ref);
        return a ? callers_of(*a) : std::vector<Function>{};
    }
    std::vector<Function> callees_of(Addr f) const { return funcs(list(h_->callees, f)); }
    std::vector<Xref> xrefs_to(Addr a) const {
        size_t n = h_->xrefs_to(p(), a, nullptr, 0);
        std::vector<rv_xref> raw(n);
        n = h_->xrefs_to(p(), a, raw.data(), n);
        std::vector<Xref> out;
        for (size_t i = 0; i < n && i < raw.size(); i++) out.push_back({raw[i].from, raw[i].to, raw[i].kind});
        return out;
    }
    std::string pseudocode(Addr f) const {
        const char* t = h_->pseudocode(p(), f);
        if (!t) throw Error(h_->last_error());
        return t;
    }

    std::vector<StringRef> strings() const {
        std::vector<StringRef> out;
        size_t n = h_->string_count(p());
        for (size_t i = 0; i < n; i++) {
            Addr a = 0;
            const char* t = nullptr;
            if (h_->string_at(p(), i, &a, &t) == 0) out.push_back({a, t});
        }
        return out;
    }
    std::vector<Addr> search_bytes(const std::string& pattern) const {
        size_t n = h_->search_bytes(p(), pattern.c_str(), nullptr, 0);
        if (n == static_cast<size_t>(-1)) throw Error(h_->last_error());
        std::vector<Addr> out(n);
        n = h_->search_bytes(p(), pattern.c_str(), out.data(), n);
        out.resize(n == static_cast<size_t>(-1) ? 0 : std::min(n, out.size()));
        return out;
    }
    std::vector<uint8_t> read(Addr a, size_t size) const {
        std::vector<uint8_t> out(size);
        out.resize(h_->read(p(), a, out.data(), size));
        return out;
    }

    void rename(Addr a, const std::string& name) const { check(h_->rename(p(), a, name.c_str())); }
    void set_comment(Addr a, const std::string& text) const { check(h_->set_comment(p(), a, text.c_str())); }
    void set_bookmark(Addr a, const std::string& label) const { check(h_->set_bookmark(p(), a, label.c_str())); }
    std::optional<std::string> comment(Addr a) const { return str(h_->comment(p(), a)); }
    std::optional<std::string> bookmark(Addr a) const { return str(h_->bookmark(p(), a)); }

    // The view lives as long as this program (and its copies) exists.
    void add_view(const std::string& name, std::function<std::string()> render) const {
        state_->views.push_back({std::move(render), {}});
        h_->add_view(p(), name.c_str(), &Program::render_view, &state_->views.back());
    }
    std::string save(const std::string& project_path = "") const {
        check(h_->save(p(), project_path.c_str()));
        return path();
    }
    void log(const std::string& message) const { h_->log(p(), message.c_str()); }

private:
    struct View {
        std::function<std::string()> render;
        std::string text;
    };
    struct State {
        rv_program* p = nullptr;
        const rv_host* host = nullptr;
        bool owned = false;
        std::list<View> views;
        ~State() {
            if (owned && p) host->close(p);  // closes the program first, so no view is called afterwards
        }
    };
    const rv_host* h_ = nullptr;
    std::shared_ptr<State> state_;

    rv_program* p() const {
        if (!valid()) throw Error("program is not open");
        return state_->p;
    }
    void check(int rc) const {
        if (rc != 0) throw Error(h_->last_error());
    }
    std::optional<Addr> opt(int (*fn)(rv_program*, rv_addr*)) const {
        Addr a = 0;
        return fn(p(), &a) == 0 ? std::optional<Addr>(a) : std::nullopt;
    }
    static std::optional<std::string> str(const char* s) { return s ? std::optional<std::string>(s) : std::nullopt; }
    std::vector<Addr> list(size_t (*fn)(rv_program*, rv_addr, rv_addr*, size_t), Addr a) const {
        size_t n = fn(p(), a, nullptr, 0);
        std::vector<Addr> out(n);
        out.resize(std::min(n, fn(p(), a, out.data(), n)));
        return out;
    }
    Function make(const rv_function_info& fi) const {
        return {fi.addr, name_of(fi.addr), fi.size, fi.insn_count, fi.block_count, fi.is_import != 0};
    }
    std::vector<Function> funcs(const std::vector<Addr>& addrs) const {
        std::vector<Function> out;
        for (Addr a : addrs) out.push_back(function(a));
        return out;
    }
    static const char* render_view(rv_program*, void* user) {
        auto* v = static_cast<View*>(user);
        try {
            v->text = v->render();
        } catch (const std::exception& e) {
            v->text = std::string("(view failed: ") + e.what() + ")";
        }
        return v->text.c_str();
    }
};

// Base class for plugins; override the hooks you need. `program` is set before on_load.
class Plugin {
public:
    virtual ~Plugin() = default;
    virtual void on_load() {}
    virtual void on_function_analyzed(const Function&) {}
    virtual void on_rename(Addr, const std::string& /*old_name*/, const std::string& /*new_name*/) {}
    virtual void on_unload() {}
    Program program;
};

namespace detail {
struct PluginState {
    std::unique_ptr<Plugin> plugin;
    std::string error;
};
template <typename F>
int guarded(void* s, F&& f) {
    auto* st = static_cast<PluginState*>(s);
    try {
        f(*st->plugin);
        return 0;
    } catch (const std::exception& e) {
        st->error = e.what();
    } catch (...) {
        st->error = "unknown C++ exception";
    }
    return 1;
}
}  // namespace detail

}  // namespace reversa

// Exports `reversa_plugin()` for a class derived from reversa::Plugin.
#define REVERSA_PLUGIN(Class, NAME, VERSION, DESCRIPTION)                                                          \
    extern "C" RV_EXPORT const rv_plugin* reversa_plugin(void) {                                                \
        using reversa::detail::PluginState;                                                                     \
        using reversa::detail::guarded;                                                                         \
        static const rv_plugin info = {                                                                         \
            RV_API_VERSION, NAME, VERSION, DESCRIPTION,                                                         \
            [](const rv_host* h, rv_program* p) -> void* {                                                      \
                try {                                                                                           \
                    auto* st = new PluginState{std::make_unique<Class>(), {}};                                  \
                    st->plugin->program = reversa::Program(h, p);                                               \
                    return st;                                                                                  \
                } catch (...) {                                                                                 \
                    return nullptr;                                                                             \
                }                                                                                               \
            },                                                                                                  \
            [](void* s) { delete static_cast<PluginState*>(s); },                                               \
            [](void* s) { return guarded(s, [](reversa::Plugin& pl) { pl.on_load(); }); },                      \
            [](void* s, rv_addr f) {                                                                            \
                return guarded(s, [f](reversa::Plugin& pl) { pl.on_function_analyzed(pl.program.function(f)); }); \
            },                                                                                                  \
            [](void* s, rv_addr a, const char* o, const char* n) {                                              \
                return guarded(s, [a, o, n](reversa::Plugin& pl) { pl.on_rename(a, o, n); });                   \
            },                                                                                                  \
            [](void* s) { return guarded(s, [](reversa::Plugin& pl) { pl.on_unload(); }); },                    \
            [](void* s) { return static_cast<PluginState*>(s)->error.c_str(); },                                \
        };                                                                                                      \
        return &info;                                                                                           \
    }

// Exports `reversa_script()`; FN is int(const rv_host*, const std::vector<std::string>& args), args[0] = script.
#define REVERSA_SCRIPT(FN)                                                                                      \
    extern "C" RV_EXPORT int reversa_script(const rv_host* host, int argc, const char* const* argv) {           \
        try {                                                                                                   \
            std::vector<std::string> args(argv, argv + argc);                                                   \
            return FN(host, args);                                                                              \
        } catch (const std::exception& e) {                                                                     \
            std::fprintf(stderr, "Error: %s\n", e.what());                                                      \
            return 1;                                                                                           \
        }                                                                                                       \
    }

#endif  // REVERSA_SDK_HPP
