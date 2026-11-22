// SQLite project format (.rvproj).
//
// Why SQLite: one portable file, transactional (no half-written projects after a crash), readable
// by any tool. The schema carries a version number; MIGRATIONS upgrade old projects step by step.
// Ghidra uses a similar "database + versioned schema" approach. Files are compatible with the
// projects written by REVERSA 0.1 (Python).
#pragma once

#include <map>
#include <string>
#include <utility>

#include "core/json.hpp"
#include "core/model.hpp"
#include "formats/formats.hpp"

namespace rv {

struct Annotations {
    std::map<Addr, std::string> names, comments, bookmarks;
    std::map<std::pair<Addr, std::string>, std::string> var_names;  // (function, var key) -> name
};

constexpr int SCHEMA_VERSION = 1;

struct ProjectData {
    std::string name, path;
    std::shared_ptr<const Bytes> data;
    LoadOptions load_options;
    Annotations annotations;
    Json settings = Json::object();
};

bool is_project(const std::string& path);
void save_project(const std::string& path, const Bytes& data, const std::string& binary_path, const LoadOptions& options,
                  const Annotations& ann, const Json& settings);  // atomic; throws ProjectError
ProjectData load_project(const std::string& path);                // throws ProjectError
// Same, with an explicit migration table (version -> SQL) and target schema; used by tests.
ProjectData load_project(const std::string& path, const std::map<int, std::string>& migrations, int schema_version);

Json load_options_json(const LoadOptions& o);
LoadOptions load_options_from_json(const Json& j);

}  // namespace rv
