// Session = one opened binary plus everything the user did with it.
//
// This is the service layer between the headless core and every front end (Python API, CLI, Qt
// GUI, plugins). All edits go through here so they are undoable, published on the event bus and
// saved with the project.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "analysis/analysis.hpp"
#include "core/json.hpp"
#include "core/model.hpp"
#include "decompiler/decompiler.hpp"
#include "formats/formats.hpp"
#include "project/store.hpp"
#include "session/events.hpp"

namespace rv {

// Implemented by the embedded Python plugin manager. Hook failures are handled by the host.
class PluginHost {
public:
    virtual ~PluginHost() = default;
    virtual bool has_hook(const std::string& hook) = 0;
    virtual void on_load() = 0;
    virtual void on_function_analyzed(Addr addr) = 0;
    virtual void on_rename(Addr addr, const std::string& old_name, const std::string& new_name) = 0;
    virtual void on_unload() = 0;
};

class Session {
public:
    explicit Session(Binary binary, LoadOptions options = {});
    static std::unique_ptr<Session> open(const std::string& path, const LoadOptions& options = {});  // binary or .rvproj
    static std::unique_ptr<Session> open_project(const std::string& path);

    Binary binary;
    LoadOptions load_options;
    std::unique_ptr<Analysis> analysis;
    Annotations annotations;
    UndoStack undo_stack;
    EventBus events;
    std::optional<std::string> project_path;
    Json settings = Json::object();
    std::vector<Diagnostic> runtime_diagnostics;
    std::shared_ptr<PluginHost> plugins;
    std::recursive_mutex lock;
    bool dirty = false;

    // analysis
    Analysis& analyze(Progress progress = nullptr, const std::atomic<bool>* cancel = nullptr);
    Analysis& require_analysis();

    // names
    std::string default_name(Addr addr) const;
    std::string name_of(Addr addr) const;
    bool has_user_name(Addr addr) const { return annotations.names.count(addr) != 0; }
    std::optional<Addr> resolve(const std::string& ref);  // "0x401000", "main", "strcmp", "strcmp@plt", "sub_401196"
    std::optional<std::string> string_text(Addr addr) const;
    bool is_symbol(int64_t v) const;

    // undoable edits (throw AnnotationError with a friendly message)
    std::string rename(const std::string& ref, const std::string& new_name);
    std::string rename(Addr addr, const std::string& new_name);
    void rename_var(const std::string& func, const std::string& key, const std::string& new_name);
    void set_comment(const std::string& ref, const std::string& text);
    void set_bookmark(const std::string& ref, const std::string& label);
    std::optional<std::string> undo();
    std::optional<std::string> redo();

    // pseudocode
    std::vector<Line> decompile(const std::string& func);
    std::vector<Line> decompile(Addr func);
    std::string pseudocode(Addr func) { return to_text(decompile(func)); }

    // search (results: address + matching text)
    using Hits = std::vector<std::pair<Addr, std::string>>;
    Hits search_strings(const std::string& query, bool regex = false, size_t min_len = 4);
    Hits search_instructions(const std::string& query, bool regex = true);
    Hits search_pseudocode(const std::string& query, bool regex = true);
    std::vector<Addr> search_bytes(const std::string& pattern) const { return rv::search_bytes(binary, pattern); }

    // custom text/Markdown views registered by plugins (shown in the GUI and in reports)
    std::vector<std::pair<std::string, std::function<std::string()>>> views;
    void add_view(const std::string& name, std::function<std::string()> render);

    // persistence
    std::string save(const std::string& path = "");
    std::vector<Diagnostic> diagnostics() const;

private:
    std::map<Addr, std::string> symbols_;
    std::optional<std::map<std::string, Addr>> name_index_;
    std::map<Addr, Decompiled> decomp_cache_;
    const std::map<std::string, Addr>& names_index();
    std::optional<Addr> resolve_addr(Addr v) const;
    void edit(const std::string& description, std::function<void()> redo, std::function<void()> undo);
    void set_map(std::map<Addr, std::string>& mapping, const std::string& topic, const std::string& label,
                 const std::string& ref, const std::string& value);
};

std::string default_project_path(const std::string& binary_path);
bool valid_name(const std::string& name);

}  // namespace rv
