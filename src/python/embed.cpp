#include "python/embed.hpp"

#include <pybind11/embed.h>

#include <cstdio>
#include <filesystem>

#include "core/text.hpp"
#include "python/bindings.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pyb = pybind11;
namespace fs = std::filesystem;

PYBIND11_EMBEDDED_MODULE(_reversa, m) { rv::py_bind::bind_all(m); }

namespace rv::py {

namespace {

enum class State { NotStarted, Ready, Failed };
State state = State::NotStarted;
std::string failure;
std::vector<pyb::object> keep_alive;  // plugin managers referenced only from C++
std::unique_ptr<pyb::gil_scoped_release> released;

fs::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}

// Folders that may contain the `reversa` Python package, in priority order.
std::vector<fs::path> package_dirs() {
    fs::path d = exe_dir();
    std::vector<fs::path> out = {d / "python", d.parent_path() / "share" / "reversa" / "python", d.parent_path() / "lib" / "reversa" / "python"};
#ifdef REVERSA_SOURCE_DIR
    out.push_back(fs::path(REVERSA_SOURCE_DIR) / "python");
#endif
    return out;
}

bool start() {
    if (state != State::NotStarted) return state == State::Ready;
    try {
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        config.parse_argv = 0;
        config.install_signal_handlers = 0;
        pyb::initialize_interpreter(&config, 0, nullptr, false);
        pyb::module_ sys = pyb::module_::import("sys");
        pyb::list path = sys.attr("path");
        for (auto& d : package_dirs()) {
            std::error_code ec;
            if (fs::is_directory(d / "reversa", ec)) {
                path.insert(0, pyb::str(path_to_utf8(d)));
                break;
            }
        }
        for (const char* stream : {"stdout", "stderr"}) {  // Windows consoles default to cp1252
            pyb::object s = sys.attr(stream);
            if (!s.is_none() && pyb::hasattr(s, "reconfigure"))
                s.attr("reconfigure")(pyb::arg("encoding") = "utf-8", pyb::arg("errors") = "replace");
        }
        pyb::module_::import("reversa");
        state = State::Ready;
    } catch (const std::exception& e) {
        state = State::Failed;
        failure = e.what();
    }
    return state == State::Ready;
}

}  // namespace

bool available(std::string* why) {
    bool ok = start();
    if (!ok && why) *why = failure;
    return ok;
}

std::string python_version() {
    if (!start()) return "";
    pyb::gil_scoped_acquire gil;
    return pyb::str(pyb::module_::import("platform").attr("python_version")()).cast<std::string>();
}

std::shared_ptr<PluginHost> make_plugin_host(Session& s, const std::vector<std::string>& extra_dirs) {
    if (!start()) {
        s.runtime_diagnostics.push_back({"warning", "Python nie je dostupný, pluginy sú vypnuté (" + failure + ").",
                                         std::nullopt, "address", failure});
        return nullptr;
    }
    pyb::gil_scoped_acquire gil;
    try {
        pyb::module_ api = pyb::module_::import("reversa.api");
        pyb::module_ plugins = pyb::module_::import("reversa.plugins");
        pyb::object session = pyb::cast(&s, pyb::return_value_policy::reference);
        pyb::object prog = api.attr("Program")(session);
        pyb::object dirs = pyb::none();
        if (!extra_dirs.empty()) {
            pyb::list d = plugins.attr("default_dirs")();
            for (auto& x : extra_dirs) d.append(x);
            dirs = d;
        }
        pyb::object manager = plugins.attr("PluginManager")(prog, dirs);
        prog.attr("plugins") = manager;
        manager.attr("load_all")();
        keep_alive.push_back(manager);
        return manager.cast<std::shared_ptr<PluginHost>>();
    } catch (const std::exception& e) {
        s.runtime_diagnostics.push_back({"error", std::string("Pluginy sa nepodarilo načítať: ") + e.what(), std::nullopt, "address", e.what()});
        return nullptr;
    }
}

Json plugin_info(const std::shared_ptr<PluginHost>& host) {
    Json out = Json::array();
    if (!host || state != State::Ready) return out;
    pyb::gil_scoped_acquire gil;
    pyb::object obj = pyb::cast(host);
    if (!pyb::hasattr(obj, "plugins")) return out;
    pyb::object json = pyb::module_::import("json");
    pyb::list items;
    for (auto p : obj.attr("plugins")) items.append(p.attr("as_dict")());
    return Json::parse(pyb::str(json.attr("dumps")(items)).cast<std::string>());
}

int run_script(const std::string& path, const std::vector<std::string>& args) {
    std::string why;
    if (!start()) {
        std::fprintf(stderr, "Chyba: Python nie je dostupný, skript sa nedá spustiť (%s).\n", failure.c_str());
        return 1;
    }
    pyb::gil_scoped_acquire gil;
    int code = 0;
    try {
        pyb::module_ sys = pyb::module_::import("sys");
        pyb::list argv;
        argv.append(path);
        for (auto& a : args) argv.append(a);
        sys.attr("argv") = argv;
        pyb::list spath = sys.attr("path");
        spath.insert(0, pyb::str(path_to_utf8(fs::absolute(path_from_utf8(path)).parent_path())));
        pyb::module_::import("runpy").attr("run_path")(path, pyb::arg("run_name") = "__main__");
    } catch (pyb::error_already_set& e) {
        if (e.matches(PyExc_SystemExit)) {
            pyb::object c = e.value().attr("code");
            code = c.is_none() ? 0 : pyb::isinstance<pyb::int_>(c) ? c.cast<int>() : 1;
            if (!c.is_none() && !pyb::isinstance<pyb::int_>(c)) pyb::print(c, pyb::arg("file") = pyb::module_::import("sys").attr("stderr"));
        } else {
            e.restore();
            PyErr_Print();
            code = 1;
        }
    }
    flush();
    return code;
}

void release_gil_for_threads() {
    if (state == State::Ready && !released) released = std::make_unique<pyb::gil_scoped_release>();
}

void flush() {
    if (state != State::Ready) return;
    pyb::gil_scoped_acquire gil;
    try {
        pyb::module_ sys = pyb::module_::import("sys");
        for (const char* s : {"stdout", "stderr"})
            if (!sys.attr(s).is_none()) sys.attr(s).attr("flush")();
    } catch (...) {
    }
}

}  // namespace rv::py
