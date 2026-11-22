#include "session/events.hpp"

#include <exception>

namespace rv {

int EventBus::subscribe(const std::string& topic, Handler h) {
    subs_.push_back({next_, topic, std::move(h)});
    return next_++;
}

void EventBus::unsubscribe(int id) {
    for (auto it = subs_.begin(); it != subs_.end(); ++it)
        if (it->id == id) {
            subs_.erase(it);
            return;
        }
}

void EventBus::publish(const std::string& topic, std::map<std::string, EventValue> data) {
    revision++;
    Event ev{topic, std::move(data)};
    std::vector<Sub> targets;  // copy: handlers may (un)subscribe while we iterate
    for (auto& s : subs_)
        if (s.topic == topic) targets.push_back(s);
    for (auto& s : subs_)
        if (s.topic == "*") targets.push_back(s);
    for (auto& s : targets) {
        try {
            s.fn(ev);
        } catch (const std::exception& e) {  // isolation: one bad listener must not break others
            if (on_error) on_error(topic, e.what());
        } catch (...) {
            if (on_error) on_error(topic, "neznáma chyba");
        }
    }
}

void UndoStack::push(Command cmd) {
    cmd.redo();
    done_.push_back(std::move(cmd));
    if (done_.size() > limit_) done_.pop_front();
    undone_.clear();
}

std::optional<std::string> UndoStack::undo() {
    if (done_.empty()) return std::nullopt;
    Command cmd = std::move(done_.back());
    done_.pop_back();
    cmd.undo();
    undone_.push_back(cmd);
    return cmd.description;
}

std::optional<std::string> UndoStack::redo() {
    if (undone_.empty()) return std::nullopt;
    Command cmd = std::move(undone_.back());
    undone_.pop_back();
    cmd.redo();
    done_.push_back(cmd);
    return cmd.description;
}

void UndoStack::clear() {
    done_.clear();
    undone_.clear();
}

}  // namespace rv
