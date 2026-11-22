// Embedded Python interpreter of the reversa application: scripts (`reversa script`) and plugins.
// The interpreter starts lazily, so the application works without Python (scripting is then off).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "session/session.hpp"

namespace rv::py {

bool available(std::string* why = nullptr);  // starts the interpreter on first call
// Loads plugins for the session (default plugin folders plus `extra_dirs`). Returns nullptr when
// Python is not available; the reason is added to the session diagnostics.
std::shared_ptr<PluginHost> make_plugin_host(Session& s, const std::vector<std::string>& extra_dirs);
// Plugin list for the GUI: [{name, version, status, error, description, hooks, calls}] as JSON.
Json plugin_info(const std::shared_ptr<PluginHost>& host);
int run_script(const std::string& path, const std::vector<std::string>& args);  // exit code
void release_gil_for_threads();  // call once on the main thread before starting worker threads
void flush();
std::string python_version();

}  // namespace rv::py
