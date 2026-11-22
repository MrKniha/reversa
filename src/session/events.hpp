// Tiny synchronous publish/subscribe bus and command-pattern undo stack.
//
// Every change to annotations (rename, comment, bookmark, ...) is published on the bus so all
// views, plugins and the GUI stay consistent. A failing subscriber is isolated: its exception is
// reported through on_error and the remaining subscribers still run.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rv {

using EventValue = std::variant<std::monostate, uint64_t, std::string>;

struct Event {
    std::string topic;
    std::map<std::string, EventValue> data;
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    std::function<void(const std::string& topic, const std::string& error)> on_error;
    uint64_t revision = 0;  // increments on every published event; views use it to refresh

    int subscribe(const std::string& topic, Handler h);  // topic "*" receives everything
    void unsubscribe(int id);
    void publish(const std::string& topic, std::map<std::string, EventValue> data = {});

private:
    struct Sub {
        int id;
        std::string topic;
        Handler fn;
    };
    std::vector<Sub> subs_;
    int next_ = 1;
};

struct Command {
    std::string description;
    std::function<void()> redo, undo;
};

class UndoStack {
public:
    explicit UndoStack(size_t limit = 1000) : limit_(limit) {}
    void push(Command cmd);  // runs cmd.redo()
    std::optional<std::string> undo();
    std::optional<std::string> redo();
    bool can_undo() const { return !done_.empty(); }
    bool can_redo() const { return !undone_.empty(); }
    void clear();

private:
    std::deque<Command> done_;
    std::vector<Command> undone_;
    size_t limit_;
};

}  // namespace rv
