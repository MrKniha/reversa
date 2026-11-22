// Automatic annotations, a custom view and a saved project:
// 1. Functions that use a string containing "password"/"flag" get a comment.
// 2. Every function that calls strcmp and has only a default name (sub_...) is renamed to
//    <name>_compares_strings.
// 3. A "strcmp calls" view is registered (GUI Views tab and reports).
// 4. The project is saved; open it in the GUI (reversa gui out.rvproj) to see the annotations.
//
//   reversa script annotate_and_view.so samples/bin/login_x64_stripped.elf out.rvproj
#include <reversa/sdk.hpp>

#include <cstdio>
#include <string>

int run(const rv_host* host, const std::vector<std::string>& args) {
    auto prog = reversa::Program::open(host, args.size() > 1 ? args[1] : "samples/bin/login_x64_stripped.elf");

    for (auto& s : prog.strings()) {
        if (s.text.find("assword") == std::string::npos && s.text.find("lag") == std::string::npos) continue;
        for (auto& x : prog.xrefs_to(s.addr)) prog.set_comment(x.from, "uses string: " + s.text.substr(0, 40));
    }
    for (auto& f : prog.callers_of("strcmp"))
        if (f.name.rfind("sub_", 0) == 0) prog.rename(f.addr, f.name + "_compares_strings");

    reversa::Program ref(prog.host(), prog.handle());  // non-owning copy: the view must not keep the program alive
    auto view = [ref] {
        const auto& prog = ref;
        std::string out = "Functions that call strcmp:\n\n";
        for (auto& f : prog.callers_of("strcmp")) {
            std::string from;
            for (auto& c : prog.callers_of(f.addr)) from += (from.empty() ? "" : ", ") + c.name;
            out += "  " + f.name + " (" + reversa::hex(f.addr) + "), called from: " + (from.empty() ? "-" : from) + "\n";
        }
        return out;
    };
    prog.add_view("strcmp calls", view);
    std::printf("%s", view().c_str());
    if (args.size() > 2) std::printf("Project saved: %s\n", prog.save(args[2]).c_str());
    return 0;
}
REVERSA_SCRIPT(run)
