// Intentionally broken plugin used by the tests and docs/plugin-tutorial.md: its hook throws, so
// REVERSA disables it, reports it, and keeps working.
#include <reversa/sdk.hpp>

#include <stdexcept>

struct CrashingPlugin : reversa::Plugin {
    void on_function_analyzed(const reversa::Function& f) override {
        throw std::runtime_error("intentional crash at function " + f.name);
    }
};

REVERSA_PLUGIN(CrashingPlugin, "crashing-plugin", "1.0.0", "Throws on purpose (isolation test).")
