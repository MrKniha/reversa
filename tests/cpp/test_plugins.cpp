// Native plugins and scripts: the C ABI host table, the C++ SDK, loading, isolation of bad plugins.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/errors.hpp"
#include "core/text.hpp"
#include "plugins/native.hpp"
#include "reversa/sdk.hpp"
#include "test.hpp"

using namespace rv;
namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
const char* LIB = ".dll";
#else
const char* LIB = ".so";
#endif

fs::path examples(const std::string& kind) { return fs::path(REVERSA_EXAMPLES_DIR) / kind; }
fs::path test_module(const std::string& kind, const std::string& name) {
    return fs::path(REVERSA_TEST_MODULES_DIR) / kind / (name + LIB);
}

fs::path temp_dir(const std::string& tag) {
    auto d = fs::temp_directory_path() / ("rvtest-plugins-" + tag + "-" + std::to_string(std::rand()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name); else setenv(name, value.c_str(), 1);
#endif
}

std::unique_ptr<Session> open_with_plugins(const std::string& sample, const std::vector<std::string>& dirs) {
    auto s = Session::open(rvtest::sample(sample));
    s->plugins = plugins::load(*s, dirs, false);
    s->analyze();
    return s;
}

std::string view(Session& s, const std::string& name) {
    for (auto& [n, render] : s.views)
        if (n == name) return render();
    return "<no view " + name + ">";
}

Json plugin_named(Session& s, const std::string& name) {
    Json all = plugins::info(s.plugins);
    for (auto& p : all.as_array())
        if (p.get("name").as_string() == name) return p;
    return Json();
}

}  // namespace

TEST("C ABI host: file, functions, xrefs, strings, search, annotations") {
    auto s = Session::open(rvtest::sample("login_x64.elf"));
    s->analyze();
    const rv_host* h = plugins::host_table();
    rv_program* p = plugins::make_program(*s);
    CHECK_EQ(h->api_version, uint32_t(RV_API_VERSION));
    CHECK_EQ(h->struct_size, uint32_t(sizeof(rv_host)));
    CHECK_EQ(std::string(h->format(p)), std::string("ELF"));
    CHECK_EQ(std::string(h->arch(p)), std::string("x86-64"));
    rv_addr main = 0, strcmp = 0, entry = 0;
    CHECK(h->main_function(p, &main) == 0);
    CHECK(h->entry(p, &entry) == 0);
    CHECK(h->resolve(p, "strcmp", &strcmp) == 0);
    CHECK(h->resolve(p, "no_such_function", &strcmp) == -1 && *h->last_error());
    CHECK(h->resolve(p, "strcmp", &strcmp) == 0);
    CHECK_EQ(std::string(h->name_of(p, main)), std::string("main"));
    uint8_t buf[4] = {};
    CHECK(h->read(p, main, buf, sizeof buf) == 4);

    size_t n = h->function_count(p);
    CHECK(n > 5);
    rv_function_info fi{}, mi{};
    CHECK(h->function_at(p, 0, &fi) == 0);
    CHECK(h->function_at(p, n, &fi) == -1);
    CHECK(h->function_info(p, main, &mi) == 0 && mi.addr == main && mi.insn_count > 0 && !mi.is_import);

    rv_addr callers[8];
    size_t nc = h->callers(p, strcmp, callers, 8);
    CHECK_EQ(nc, size_t(2));
    std::vector<std::string> names;
    for (size_t i = 0; i < nc; i++) names.push_back(h->name_of(p, callers[i]));
    std::sort(names.begin(), names.end());
    CHECK((names == std::vector<std::string>{"check_password", "check_user"}));
    CHECK(h->callers(p, strcmp, nullptr, 0) == 2);  // count only
    CHECK(h->callees(p, main, nullptr, 0) >= 3);
    rv_xref x[16];
    size_t nx = h->xrefs_to(p, strcmp, x, 16);
    CHECK(nx >= 2 && std::string(x[0].kind) == "call");
    CHECK(contains(h->pseudocode(p, main), "check_password"));

    bool admin = false;
    for (size_t i = 0; i < h->string_count(p); i++) {
        rv_addr a; const char* t;
        CHECK(h->string_at(p, i, &a, &t) == 0);
        if (std::string(t) == "admin") admin = true;
    }
    CHECK(admin);
    rv_addr hits[4];
    CHECK(h->search_bytes(p, "zz", hits, 4) == size_t(-1) && contains(h->last_error(), "hexadecimal"));

    CHECK(h->rename(p, main, "entry_point") == 0);
    CHECK_EQ(std::string(h->name_of(p, main)), std::string("entry_point"));
    CHECK(h->rename(p, main, "bad name!") == -1);
    CHECK(h->set_comment(p, main, "start") == 0 && std::string(h->comment(p, main)) == "start");
    CHECK(h->set_bookmark(p, main, "here") == 0 && std::string(h->bookmark(p, main)) == "here");
    CHECK(h->set_comment(p, main, "") == 0 && h->comment(p, main) == nullptr);
    CHECK(s->undo().has_value());  // edits from plugins are undoable like the user's
    plugins::free_program(p);
}

TEST("C++ SDK wraps the host: open, functions, callers, owned and borrowed programs") {
    auto prog = reversa::Program::open(plugins::host_table(), rvtest::sample("login_x64.elf"));
    CHECK_EQ(prog.format(), std::string("ELF"));
    auto main = prog.main();
    CHECK(main.has_value() && main->name == "main");
    std::vector<std::string> callers;
    for (auto& f : prog.callers_of("strcmp")) callers.push_back(f.name);
    std::sort(callers.begin(), callers.end());
    CHECK((callers == std::vector<std::string>{"check_password", "check_user"}));
    CHECK(!prog.functions().empty());
    CHECK(!prog.strings().empty());
    bool threw = false;
    try { prog.addr("no_such_function"); } catch (const reversa::Error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { reversa::Program::open(plugins::host_table(), "/no/such/file.elf"); } catch (const reversa::Error& e) {
        threw = contains(e.what(), "file.elf");
    }
    CHECK(threw);
}

TEST("example plugin: all hooks run, views and comments appear, renames are seen") {
    auto dir = temp_dir("hello");
    fs::copy_file(examples("plugins") / (std::string("hello_plugin") + LIB), dir / (std::string("hello_plugin") + LIB));
    auto s = open_with_plugins("login_x64.elf", {dir.string()});
    Json p = plugin_named(*s, "hello-plugin");
    CHECK_EQ(p.get("status").as_string(), std::string("active"));
    CHECK_EQ(p.get("hooks").size(), size_t(4));
    CHECK(contains(s->annotations.comments[*s->resolve("main")], "hello-plugin"));
    s->rename("check_user", "verify");
    CHECK(contains(view(*s, "Hello plugin"), "Renames: 1"));
    CHECK(contains(view(*s, "Hello plugin"), "Functions analysed: "));
    s.reset();  // on_unload runs, nothing crashes
    fs::remove_all(dir);
}

TEST("plugin written in C: create, hooks and a view") {
    auto s = open_with_plugins("login_x64.elf", {test_module("plugins", "counter").parent_path().string()});
    Json p = plugin_named(*s, "counter");
    CHECK_EQ(p.get("status").as_string(), std::string("active"));
    s->rename("check_user", "verify");
    std::string v = view(*s, "Counter");
    CHECK(contains(v, "renames=1") && contains(v, "last=verify"));
    CHECK(!contains(v, "functions=0"));
}

TEST("bad plugins are reported and never stop loading") {
    auto s = open_with_plugins("hello_x64.elf", {test_module("plugins", "x").parent_path().string(),
                                                 examples("plugins").string()});
    // garbage file with a library extension
    auto dir = temp_dir("bad");
    { std::ofstream(dir / (std::string("garbage") + LIB)) << "not a library"; }
    auto s2 = open_with_plugins("hello_x64.elf", {dir.string()});
    CHECK_EQ(plugin_named(*s2, "garbage").get("status").as_string(), std::string("failed"));
    CHECK(contains(plugin_named(*s2, "garbage").get("error").as_string(), "cannot open"));

    CHECK_EQ(plugin_named(*s, "old-api").get("status").as_string(), std::string("incompatible"));
    CHECK_EQ(plugin_named(*s, "null_entry").get("status").as_string(), std::string("failed"));
    CHECK(contains(plugin_named(*s, "no_entry").get("error").as_string(), "reversa_plugin()"));
    Json fl = plugin_named(*s, "failing-load");
    CHECK(fl.get("status").as_string() == "failed" && contains(fl.get("error").as_string(), "nope, not today"));
    Json crash = plugin_named(*s, "crashing-plugin");
    CHECK(crash.get("status").as_string() == "failed" && contains(crash.get("error").as_string(), "intentional crash"));
    CHECK_EQ(plugin_named(*s, "counter").get("status").as_string(), std::string("active"));
    CHECK_EQ(plugin_named(*s, "hello-plugin").get("status").as_string(), std::string("active"));
    bool reported = false;
    for (auto& d : s->diagnostics())
        if (contains(d.message, "crashing-plugin") && contains(d.message, "REVERSA continues")) reported = true;
    CHECK(reported);
    CHECK(s->require_analysis().functions.size() > 5);  // the application keeps working
    CHECK(!s->pseudocode(*s->resolve("main")).empty());
    fs::remove_all(dir);
}

TEST("crypto plugin from REVERSA_PLUGIN_PATH finds AES on a raw blob") {
    auto dir = temp_dir("aes");
    std::string blob = std::string(64, '\x90') + "\x63\x7c\x77\x7b\xf2\x6b\x6f\xc5\x30\x01\x67\x2b\xfe\xd7\xab\x76" +
                       std::string(16, '\xc3');
    { std::ofstream(dir / "aes.bin", std::ios::binary) << blob; }
    set_env("REVERSA_PLUGIN_PATH", examples("plugins").string());
    LoadOptions o;
    o.arch = "x86-64";
    auto s = Session::open((dir / "aes.bin").string(), o);
    s->plugins = plugins::load(*s, {}, true);
    s->analyze();
    set_env("REVERSA_PLUGIN_PATH", "");
    CHECK(contains(view(*s, "Crypto constants"), "AES S-box"));
    bool bookmark = false;
    for (auto& [a, label] : s->annotations.bookmarks)
        if (contains(label, "crypto: AES")) bookmark = true;
    CHECK(bookmark);
    s.reset();
    fs::remove_all(dir);
}

TEST("no plugin folders, no plugins") {
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    s->plugins = plugins::load(*s, {}, false);
    CHECK_EQ(plugins::info(s->plugins).size(), size_t(0));
}

TEST("scripts: run, arguments, exit codes, friendly errors") {
    CHECK_EQ(plugins::run_script(test_module("scripts", "exit_code").string(), {"a", "b"}), 3);
    CHECK_EQ(plugins::run_script((examples("scripts") / (std::string("strcmp_callers") + LIB)).string(),
                                 {rvtest::sample("login_x64.elf")}), 0);
    auto err = [](const std::string& path) {
        try { plugins::run_script(path, {}); } catch (const ReversaError& e) { return std::string(e.what()); }
        return std::string("<no error>");
    };
    CHECK(contains(err("old.py"), "Python scripts are no longer supported"));
    CHECK(contains(err("/no/such/script" + std::string(LIB)), "does not exist"));
    CHECK(contains(err(test_module("plugins", "no_entry").string()), "reversa_script()"));
}
