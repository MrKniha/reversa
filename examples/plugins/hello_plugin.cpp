// Example plugin from docs/plugin-tutorial.md: counts functions, comments main, reacts to renames.
//
// Build (Linux):   g++ -std=c++17 -shared -fPIC -I<sdk>/include hello_plugin.cpp -o hello_plugin.so
// Install:         copy hello_plugin.so to ~/.reversa/plugins/ (Windows: %USERPROFILE%\.reversa\plugins\)
#include <reversa/sdk.hpp>

#include <string>
#include <vector>

struct HelloPlugin : reversa::Plugin {
    int functions = 0;
    std::vector<std::string> renames;

    void on_load() override {
        program.log("[hello-plugin] loaded file: " + program.path());
        program.add_view("Hello plugin", [this] {
            return "Functions analysed: " + std::to_string(functions) + "\nRenames: " + std::to_string(renames.size());
        });
    }
    void on_function_analyzed(const reversa::Function& f) override {
        functions++;
        if (f.name == "main") program.set_comment(f.addr, "hello-plugin: the program starts here");
    }
    void on_rename(reversa::Addr, const std::string& old_name, const std::string& new_name) override {
        renames.push_back(old_name + " -> " + new_name);
        program.log("[hello-plugin] " + old_name + " -> " + new_name);
    }
    void on_unload() override { program.log("[hello-plugin] saw " + std::to_string(functions) + " functions"); }
};

REVERSA_PLUGIN(HelloPlugin, "hello-plugin", "1.0.0", "Example from docs/plugin-tutorial.md: counts functions and reacts to renames.")
