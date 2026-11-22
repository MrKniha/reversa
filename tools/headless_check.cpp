// Headless end-to-end check used by CI on Linux and Windows (standalone, standard C++ only):
// analyses every sample, writes reports and projects, runs the demo and the strcmp example script.
//
// Usage: headless_check REVERSA_EXE REPO_ROOT [OUTPUT_DIR] [SCRIPT]
//   SCRIPT defaults to <folder of REVERSA_EXE>/examples/scripts/strcmp_callers.so|.dll
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path exe, out;

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string run(const std::vector<std::string>& args) {
    fs::path log = out / "last-run.txt";
    std::string cmd = "\"" + exe.string() + "\"", shown = "reversa";
    for (auto& a : args) { cmd += " \"" + a + "\""; shown += " " + a; }
    cmd += " >\"" + log.string() + "\" 2>&1";
#ifdef _WIN32
    int code = std::system(("\"" + cmd + "\"").c_str());
#else
    int status = std::system(cmd.c_str());
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    std::string text = slurp(log);
    if (code != 0) {
        std::fprintf(stderr, "FAILED (exit code %d): %s\n%s\n", code, shown.c_str(), text.c_str());
        std::exit(1);
    }
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: headless_check REVERSA_EXE REPO_ROOT [OUTPUT_DIR] [SCRIPT]\n");
        return 2;
    }
    exe = fs::absolute(argv[1]);
    fs::path root = fs::absolute(argv[2]);
    out = fs::absolute(argc > 3 ? argv[3] : "headless-out");
#ifdef _WIN32
    const char* lib = ".dll";
#else
    const char* lib = ".so";
#endif
    fs::path script = argc > 4 ? fs::path(argv[4]) : exe.parent_path() / "examples" / "scripts" / (std::string("strcmp_callers") + lib);
    fs::create_directories(out);

    std::vector<fs::path> samples;
    for (auto& e : fs::directory_iterator(root / "samples" / "bin"))
        if (e.path().extension() == ".elf" || e.path().extension() == ".exe") samples.push_back(e.path());
    std::sort(samples.begin(), samples.end());
    if (samples.empty()) { std::fprintf(stderr, "no samples found in %s\n", (root / "samples" / "bin").string().c_str()); return 1; }
    for (auto& f : samples) {
        std::string name = f.filename().string();
        run({"analyze", f.string(), "--report", (out / (name + ".md")).string(), "--project",
             (out / (name + ".rvproj")).string(), "--no-plugins", "--fail-on-error"});
    }
    run({"analyze", (root / "samples" / "bin" / "login_x64.elf").string(), "--report", (out / "login.html").string(), "--no-plugins"});
    run({"--demo", "--no-gui"});
    std::string callers = run({"script", script.string(), (root / "samples" / "bin" / "login_x64.exe").string()});
    if (callers.find("check_password") == std::string::npos || callers.find("check_user") == std::string::npos) {
        std::fprintf(stderr, "FAILED: the strcmp script did not find check_password and check_user:\n%s\n", callers.c_str());
        return 1;
    }
    std::printf("headless check OK: %zu samples, output in %s\n", samples.size(), out.string().c_str());
    return 0;
}
