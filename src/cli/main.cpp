// Command-line interface: `reversa`.
//
//   reversa --demo                 open the demo binary in the GUI (add --no-gui for terminal output)
//   reversa gui [FILE]             start the GUI
//   reversa analyze FILE [--report out.md|out.html] [--json] [--project out.rvproj] [--pseudocode FUNC]
//   reversa info FILE              quick header information
//   reversa search FILE (--string|--hex|--insn|--code) QUERY
//   reversa script SCRIPT.py [ARGS...]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cli/cli.hpp"
#include "core/errors.hpp"
#include "core/text.hpp"
#include "report/report.hpp"
#include "session/session.hpp"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define RV_ISATTY(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define RV_ISATTY(f) isatty(fileno(f))
#endif

#ifdef REVERSA_WITH_PYTHON
#include "python/embed.hpp"
#endif
#ifdef REVERSA_WITH_GUI
#include "gui/gui.hpp"
#endif

extern const unsigned char rv_demo_elf[];
extern const std::size_t rv_demo_elf_size;

namespace fs = std::filesystem;

namespace rv::cli {

namespace {

constexpr int EXIT_OK = 0, EXIT_ERROR = 1, EXIT_USAGE = 2;

const char* HELP = R"(usage: reversa [-h] [--version] [--demo] [--no-gui] {analyze,gui,info,search,script} ...

REVERSA: open-source reverse engineering tool.

commands:
  analyze FILE        analyse a file without the GUI (headless)
      --report FILE       save a report (.md or .html)
      --json              print the summary as JSON
      --project FILE      save a project (.rvproj)
      --pseudocode FUNC   print the pseudocode of a function (name or address)
      --fail-on-error     exit code 1 if loading reported errors
  gui [FILE]          start the graphical interface
  info FILE           basic information about a file
  search FILE         search a file: --string TEXT [--regex] | --hex '48 8b ?? 05' |
                      --insn REGEX | --code REGEX (regex over pseudocode)
  script SCRIPT [ARGS...]   run a Python script with the reversa module available

common options:
  --arch {x86,x86-64,arm,arm64}   set the architecture by hand (for raw data)
  --base ADDR                     base address for raw data, e.g. 0x8000000
  --raw                           load as raw data (ignore headers)
  --plugins DIR                   plugin folder (may be repeated)
  --no-plugins                    load no plugins

other options:
  --demo              run the sample analysis (in the GUI; with --no-gui in the terminal)
  --no-gui            with --demo, print the results to the terminal
  --version           print the version
)";

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::optional<uint64_t> parse_int(const std::string& v) {  // Python int(v, 0)
    try {
        size_t used = 0;
        uint64_t x;
        if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) x = std::stoull(v.substr(2), &used, 16), used += 2;
        else if (v.size() > 2 && v[0] == '0' && (v[1] == 'o' || v[1] == 'O')) x = std::stoull(v.substr(2), &used, 8), used += 2;
        else if (v.size() > 2 && v[0] == '0' && (v[1] == 'b' || v[1] == 'B')) x = std::stoull(v.substr(2), &used, 2), used += 2;
        else x = std::stoull(v, &used, 10);
        if (used != v.size()) return std::nullopt;
        return x;
    } catch (...) {
        return std::nullopt;
    }
}

Args parse(const std::vector<std::string>& argv) {
    Args a;
    auto need = [&](size_t& i, const std::string& opt) -> std::string {
        if (i + 1 >= argv.size()) throw UsageError("option " + opt + " requires a value");
        return argv[++i];
    };
    for (size_t i = 0; i < argv.size(); i++) {
        const std::string& t = argv[i];
        if (a.cmd == "script" && !a.file.empty()) {  // everything after the script belongs to it
            a.script_args.push_back(t);
            continue;
        }
        if (t == "-h" || t == "--help") a.help = true;
        else if (t == "--version") a.version = true;
        else if (t == "--demo") a.demo = true;
        else if (t == "--no-gui") a.no_gui = true;
        else if (t == "--json") a.json = true;
        else if (t == "--raw") a.options.force_raw = true;
        else if (t == "--regex") a.regex = true;
        else if (t == "--no-plugins") a.no_plugins = true;
        else if (t == "--fail-on-error") a.fail_on_error = true;
        else if (t == "--report") a.report = need(i, t);
        else if (t == "--project") a.project = need(i, t);
        else if (t == "--pseudocode") a.pseudocode = need(i, t);
        else if (t == "--plugins") a.plugin_dirs.push_back(need(i, t));
        else if (t == "--arch") {
            std::string v = need(i, t);
            if (v != "x86" && v != "x86-64" && v != "arm" && v != "arm64")
                throw UsageError("invalid architecture '" + v + "' (choices: x86, x86-64, arm, arm64)");
            a.options.arch = v;
        } else if (t == "--base") {
            std::string v = need(i, t);
            auto x = parse_int(v);
            if (!x) throw UsageError("invalid address '" + v + "'");
            a.options.base = *x;
        } else if (t == "--string" || t == "--hex" || t == "--insn" || t == "--code") {
            if (!a.search_kind.empty()) throw UsageError("use only one of --string, --hex, --insn, --code");
            a.search_kind = t.substr(2);
            a.query = need(i, t);
        } else if (starts_with(t, "-") && t.size() > 1) {
            throw UsageError("unknown option: " + t);
        } else if (a.cmd.empty() && !a.demo) {
            if (t != "analyze" && t != "gui" && t != "info" && t != "search" && t != "script") {
                std::error_code ec;
                if (!fs::is_regular_file(path_from_utf8(t), ec)) throw UsageError("unknown command '" + t + "'");
                a.cmd = "gui";  // `reversa FILE` (e.g. "Open with") opens the file in the GUI
                a.file = t;
                continue;
            }
            a.cmd = t;
        } else if (a.file.empty()) {
            a.file = t;
        } else {
            throw UsageError("unexpected argument: " + t);
        }
    }
    if ((a.cmd == "analyze" || a.cmd == "info" || a.cmd == "search" || a.cmd == "script") && a.file.empty())
        throw UsageError("command " + a.cmd + " requires a file");
    if (a.cmd == "search" && a.search_kind.empty())
        throw UsageError("search requires one of --string, --hex, --insn, --code");
    return a;
}

void progress(double frac, const std::string& msg) {
    std::string m = msg;
    while (utf8_length(m) < 50) m += ' ';
    std::fprintf(stderr, "\r[%3d %%] %s", int(frac * 100), m.c_str());
    if (frac >= 1) std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void attach_plugins(Session& s, const Args& a) {
#ifdef REVERSA_WITH_PYTHON
    if (a.no_plugins) return;
    s.plugins = py::make_plugin_host(s, a.plugin_dirs);
#else
    (void)s;
    if (!a.plugin_dirs.empty())
        std::fprintf(stderr, "Warning: this REVERSA build has no Python, plugins are not loaded.\n");
#endif
}

void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path_from_utf8(path), std::ios::binary);
    if (!f) throw ReversaError("Could not write the file '" + path + "'.");
    f << text;
}

int cmd_analyze(const Args& a) {
    auto s = open_session(a);
    s->analyze(RV_ISATTY(stderr) ? Progress(progress) : nullptr);
    Json info = summary(*s);
    if (a.json) {
        std::cout << info.dump(2) << "\n";
    } else {
        std::cout << "File:         " << info.get("file").as_string() << "\n";
        std::cout << "Format:       " << info.get("format").as_string() << " / " << info.get("arch").as_string() << " ("
                  << info.get("bits").as_int() << "-bit, " << info.get("endian").as_string() << ")\n";
        std::cout << "Compiler:     " << info.get("compiler").as_string() << "\n";
        std::cout << "Functions:    " << info.get("functions").as_int() << " (+ " << info.get("imports").as_int()
                  << " imports), strings: " << info.get("strings").as_int() << ", x-refs: " << info.get("xrefs").as_int() << "\n";
        std::string main = info.get("main").is_null() ? "not found" : info.get("main").as_string();
        std::cout << "Main func.:   " << main << "   (analysis " << info.get("seconds").dump() << " s)\n";
        for (auto& d : info.get("diagnostics").as_array())
            std::cout << "  ! " << d.get("severity").as_string() << ": " << d.get("message").as_string() << "\n";
    }
    if (!a.pseudocode.empty()) {
        auto f = s->resolve(a.pseudocode);
        if (!f) throw ReversaError("'" + a.pseudocode + "' not found (neither as a name nor as an address).");
        auto* fn = s->analysis->functions.count(*f) ? &s->analysis->functions.at(*f) : s->analysis->function_containing(*f);
        if (!fn) throw ReversaError("There is no function at " + format_hex(*f) + ".");
        std::cout << s->pseudocode(fn->addr) << "\n";
    }
    if (!a.report.empty()) {
        write_file(a.report, build_report(*s, ends_with(a.report, ".html") ? "html" : "md"));
        std::cout << "Report saved: " << path_to_utf8(fs::absolute(path_from_utf8(a.report))) << "\n";
    }
    if (!a.project.empty()) std::cout << "Project saved: " << s->save(a.project) << "\n";
    if (a.fail_on_error)
        for (auto& d : info.get("diagnostics").as_array())
            if (d.get("severity").as_string() == "error") return EXIT_ERROR;
    return EXIT_OK;
}

int cmd_info(const Args& a) {
    auto s = open_session(a);
    const Binary& b = s->binary;
    std::cout << b.format << " " << b.arch << " " << b.bits << "-bit " << b.endian
              << ", entry=" << (b.entry && *b.entry ? format_hex(*b.entry) : "-") << ", compiler: " << b.compiler << "\n";
    for (auto& sec : b.sections) {
        char line[256];
        std::snprintf(line, sizeof line, "  %-20s 0x%-12llx %10llu  %s\n", sec.name.c_str(), (unsigned long long)sec.addr,
                      (unsigned long long)sec.size, sec.perms().c_str());
        std::cout << line;
    }
    for (auto& d : s->diagnostics()) std::cout << "  ! " << d.friendly() << "\n";
    return EXIT_OK;
}

int cmd_search(const Args& a) {
    auto s = open_session(a);
    s->analyze();
    Session::Hits hits;
    if (a.search_kind == "string") hits = s->search_strings(a.query, a.regex);
    else if (a.search_kind == "hex")
        for (Addr x : s->search_bytes(a.query)) hits.emplace_back(x, a.query);
    else if (a.search_kind == "insn") hits = s->search_instructions(a.query);
    else hits = s->search_pseudocode(a.query);
    for (auto& [addr, text] : hits) std::cout << format_hex(addr) << "  " << text << "\n";
    std::cerr << hits.size() << " results\n";
    return EXIT_OK;
}

int cmd_demo_terminal() {
    auto t0 = std::chrono::steady_clock::now();
    auto s = open_demo();
    s->analyze();
    Json info = summary(*s);
    std::cout << "REVERSA demo: sample binary 'login' (CTF task: find the password)\n\n";
    std::cout << "Format " << info.get("format").as_string() << " " << info.get("arch").as_string() << ", "
              << info.get("functions").as_int() << " functions, " << info.get("strings").as_int() << " strings, analysis "
              << info.get("seconds").dump() << " s\n\n";
    std::cout << "Functions that call strcmp (string comparison):\n";
    if (auto cs = s->resolve("strcmp"))
        for (Addr c : s->analysis->callers(*cs)) {
            std::string n = s->name_of(c);
            while (utf8_length(n) < 20) n += ' ';
            std::cout << "  " << n << " " << format_hex(c) << "\n";
        }
    Addr main = s->analysis->main.value_or(0);
    std::cout << "\nPseudocode of the main function '" << s->name_of(main) << "':\n\n" << s->pseudocode(main) << "\n";
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", secs);
    std::cout << "\nDone in " << buf << " s. Start the GUI with: reversa --demo\n";
    return EXIT_OK;
}

int cmd_script(const Args& a) {
#ifdef REVERSA_WITH_PYTHON
    return py::run_script(a.file, a.script_args);
#else
    (void)a;
    throw ReversaError("This REVERSA build has no Python, scripts are not available.",
                       "build with -DREVERSA_PYTHON=ON");
#endif
}

int cmd_gui(const Args& a) {
#ifdef REVERSA_WITH_GUI
    if (!gui::display_available())
        throw ReversaError("Cannot start the graphical interface: no display is available (neither DISPLAY nor WAYLAND_DISPLAY). "
                           "Use the analyze command or --demo --no-gui.");
    return gui::run(a);
#else
    (void)a;
    throw ReversaError("This REVERSA build has no graphical interface. Use --no-gui or the analyze command.",
                       "build with -DREVERSA_GUI=ON");
#endif
}

}  // namespace

std::unique_ptr<Session> open_session(const Args& a) {
    auto s = Session::open(a.file, a.options);
    attach_plugins(*s, a);
    return s;
}

std::unique_ptr<Session> open_demo() {
    auto data = std::make_shared<Bytes>(rv_demo_elf, rv_demo_elf + rv_demo_elf_size);
    return std::make_unique<Session>(load_bytes(data, "<demo>/login_x64.elf"));
}

static int run(const std::vector<std::string>& argv);

int main(const std::vector<std::string>& argv) {
    int code = run(argv);
#ifdef REVERSA_WITH_PYTHON
    py::flush();  // output printed by plugins
#endif
    return code;
}

static int run(const std::vector<std::string>& argv) {
    Args a;
    try {
        a = parse(argv);
    } catch (const UsageError& e) {
        std::cerr << "reversa: error: " << e.what() << "\nHelp: reversa --help\n";
        return EXIT_USAGE;
    }
    if (a.help) {
        std::cout << HELP;
        return EXIT_OK;
    }
    if (a.version) {
        std::cout << "REVERSA " << REVERSA_VERSION << "\n";
        return EXIT_OK;
    }
    try {
        if (a.demo && a.no_gui) return cmd_demo_terminal();
        if (a.demo || a.cmd == "gui") return cmd_gui(a);
        if (a.cmd == "analyze") return cmd_analyze(a);
        if (a.cmd == "info") return cmd_info(a);
        if (a.cmd == "search") return cmd_search(a);
        if (a.cmd == "script") return cmd_script(a);
#ifdef REVERSA_WITH_GUI
        if (argv.empty() && gui::display_available()) return cmd_gui(a);  // started without arguments (double click): open the GUI
#endif
        std::cout << HELP;
        return EXIT_USAGE;
    } catch (const ReversaError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        if (!e.detail.empty()) {
            auto lines = split(trim(e.detail), '\n');
            std::cerr << "Details for the maintainer: " << (lines.empty() ? "" : lines.back()) << "\n";
        }
        return EXIT_ERROR;
    } catch (const std::exception& e) {  // last resort: friendly message + details
        std::cerr << "Unexpected error: " << e.what()
                  << "\nPlease report it (SECURITY.md / issues) with these details.\n";
        return EXIT_ERROR;
    }
}

}  // namespace rv::cli

