// Native plugins and scripts: shared libraries using the C ABI in sdk/include/reversa/plugin.h.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "reversa/plugin.h"
#include "session/session.hpp"

namespace rv::plugins {

// ~/.reversa/plugins plus the folders in REVERSA_PLUGIN_PATH.
std::vector<std::string> default_dirs();
// Loads every plugin library found in the default folders (if `use_default_dirs`) and `extra_dirs`.
// Load problems become session diagnostics; the returned host is never null.
std::shared_ptr<PluginHost> load(Session& s, const std::vector<std::string>& extra_dirs, bool use_default_dirs = true);
// Plugin list for the GUI: [{name, version, path, description, status, error, hooks, calls}].
Json info(const std::shared_ptr<PluginHost>& host);
// `reversa script LIBRARY [ARGS...]`: runs reversa_script() from the library; returns its exit code.
int run_script(const std::string& path, const std::vector<std::string>& args);

// The function table handed to plugins and scripts, and handles for sessions (used by tests).
const rv_host* host_table();
rv_program* make_program(Session& s);  // not owning; free with free_program
void free_program(rv_program* p);

}  // namespace rv::plugins
