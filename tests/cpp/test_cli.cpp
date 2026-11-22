// The `reversa` command line program, run as a child process.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/json.hpp"
#include "core/text.hpp"
#include "test.hpp"
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
using rv::contains;

namespace {

#ifdef _WIN32
const char* LIB = ".dll";
#else
const char* LIB = ".so";
#endif

struct Run { int code; std::string out, err; };

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

fs::path work_dir() {
    static fs::path d = [] {
        auto p = fs::temp_directory_path() / ("rvtest-cli-" + std::to_string(std::rand()));
        fs::remove_all(p);
        fs::create_directories(p / "home");
        // the child must not see the developer's ~/.reversa/plugins
#ifdef _WIN32
        _putenv_s("USERPROFILE", (p / "home").string().c_str());
        _putenv_s("REVERSA_PLUGIN_PATH", "");
#else
        setenv("HOME", (p / "home").c_str(), 1);
        unsetenv("REVERSA_PLUGIN_PATH");
#endif
        return p;
    }();
    return d;
}

Run run(const std::vector<std::string>& args) {
    fs::path out = work_dir() / "stdout.txt", err = work_dir() / "stderr.txt";
    std::string cmd = "\"" + std::string(REVERSA_EXE) + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";
    cmd += " >\"" + out.string() + "\" 2>\"" + err.string() + "\"";
#ifdef _WIN32
    int code = std::system(("\"" + cmd + "\"").c_str());
#else
    int status = std::system(cmd.c_str());
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return {code, slurp(out), slurp(err)};
}

std::string tmp(const std::string& name) { return (work_dir() / name).string(); }

}  // namespace

TEST("cli: analyze writes Markdown, HTML, JSON and a project") {
    auto r = run({"analyze", rvtest::sample("login_x64.elf"), "--report", tmp("report.md"), "--no-plugins"});
    CHECK_EQ(r.code, 0);
    std::string md = slurp(tmp("report.md"));
    CHECK(contains(md, "# Analysis: login_x64.elf") && contains(md, "check_password") && contains(md, "```c"));
    CHECK(contains(r.out, "Main func.:   main"));

    r = run({"analyze", rvtest::sample("hello_arm64.elf"), "--report", tmp("r.html"), "--json", "--project",
             tmp("p.rvproj"), "--pseudocode", "sum_to", "--no-plugins"});
    CHECK_EQ(r.code, 0);
    auto end = r.out.find("\n}");
    CHECK(end != std::string::npos);
    auto info = rv::Json::parse(r.out.substr(0, end + 2));
    CHECK(info.get("arch").as_string() == "arm64" && info.get("main").as_string() == "main");
    CHECK(info.get("functions").as_int() > 5);
    CHECK(contains(r.out, "while ("));
    CHECK(rv::starts_with(slurp(tmp("r.html")), "<!doctype html>"));
    CHECK(fs::exists(tmp("p.rvproj")));
}

TEST("cli: raw firmware with a manual architecture") {
    const char raw[] = "\xfd\x7b\xbf\xa9\xfd\x03\x00\x91\x20\x00\x80\xd2\xfd\x7b\xc1\xa8\xc0\x03\x5f\xd6";
    std::string bytes(raw, sizeof raw - 1);
    { std::ofstream(tmp("fw.bin"), std::ios::binary) << bytes; }
    auto r = run({"analyze", tmp("fw.bin"), "--arch", "arm64", "--base", "0x8000", "--pseudocode", "0x8000", "--no-plugins"});
    CHECK(r.code == 0 && contains(r.out, "RAW / arm64") && contains(r.out, "return 1;"));
}

TEST("cli: demo, info, search") {
    auto r = run({"--demo", "--no-gui"});
    CHECK(r.code == 0 && contains(r.out, "check_password") && contains(r.out, "Access granted"));
    CHECK(contains(run({"info", rvtest::sample("hello_x64.exe"), "--no-plugins"}).out, ".text"));
    CHECK(contains(run({"search", rvtest::sample("login_x64.elf"), "--string", "admin", "--no-plugins"}).out, "admin"));
    for (auto args : std::vector<std::vector<std::string>>{{"--hex", "f3 0f 1e fa"}, {"--insn", "call.*"}, {"--code", "strcmp\\("}}) {
        std::vector<std::string> a = {"search", rvtest::sample("login_x64.elf")};
        a.insert(a.end(), args.begin(), args.end());
        a.push_back("--no-plugins");
        r = run(a);
        CHECK(r.code == 0 && !r.out.empty() && contains(r.err, "results"));
    }
}

TEST("cli: C++ scripts and plugins from the command line") {
    fs::path ex = REVERSA_EXAMPLES_DIR;
    auto r = run({"script", (ex / "scripts" / (std::string("strcmp_callers") + LIB)).string(), rvtest::sample("login_x64.elf")});
    CHECK(r.code == 0 && contains(r.out, "check_password") && contains(r.out, "check_user"));
    r = run({"script", (fs::path(REVERSA_TEST_MODULES_DIR) / "scripts" / (std::string("exit_code") + LIB)).string(), "x"});
    CHECK(r.code == 3 && contains(r.out, "argc=2 x"));
    r = run({"script", tmp("old.py")});
    CHECK(r.code == 1 && contains(r.err, "Python scripts are no longer supported"));

    r = run({"analyze", rvtest::sample("login_x64.elf"), "--plugins", (ex / "plugins").string(), "--report", tmp("pl.md")});
    CHECK_EQ(r.code, 0);
    CHECK(contains(r.out, "[hello-plugin] loaded file"));
    CHECK(contains(r.out + r.err, "crashing-plugin") && contains(r.out + r.err, "REVERSA continues"));
    std::string md = slurp(tmp("pl.md"));
    CHECK(contains(md, "## View: Hello plugin") && contains(md, "Functions analysed: "));
}

TEST("cli: errors are friendly, exit codes are right") {
    auto r = run({"analyze", tmp("missing.exe")});
    CHECK(r.code == 1 && contains(r.err, "Error: File"));
    { std::ofstream(tmp("bad.elf"), std::ios::binary) << std::string("\x7f" "ELF") + std::string(80, '\x09'); }
    r = run({"analyze", tmp("bad.elf"), "--no-plugins"});
    CHECK(r.code == 0 && contains(r.out, "damaged"));
    CHECK_EQ(run({"analyze", tmp("bad.elf"), "--no-plugins", "--fail-on-error"}).code, 1);
#ifndef _WIN32  // without arguments and without a display: help + usage error (Windows opens the GUI)
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) CHECK_EQ(run({}).code, 2);
#endif
    r = run({"analyze"});
    CHECK(r.code == 2 && contains(r.err, "requires a file"));
    CHECK(rv::starts_with(run({"--version"}).out, "REVERSA "));
}
