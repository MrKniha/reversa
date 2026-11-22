// Parsed command line, shared by the CLI and the GUI entry point.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "formats/formats.hpp"
#include "session/session.hpp"

namespace rv::cli {

struct Args {
    std::string cmd, file;
    bool help = false, version = false, demo = false, no_gui = false, json = false, regex = false;
    bool no_plugins = false, fail_on_error = false;
    std::string report, project, pseudocode, search_kind, query;
    std::vector<std::string> plugin_dirs, script_args;
    LoadOptions options;
};

std::unique_ptr<Session> open_session(const Args& a);  // binary or project, with plugins attached
std::unique_ptr<Session> open_demo();
int main(const std::vector<std::string>& argv);

}  // namespace rv::cli
