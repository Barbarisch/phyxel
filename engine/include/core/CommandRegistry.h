#pragma once

#include "core/APICommandQueue.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace Phyxel {
namespace Core {

// Maps an APICommand action name to a handler that fills the JSON response. This lets command
// handlers be registered one line per command (grouped by domain) instead of living in the giant
// if-chain in Application::processAPICommands. dispatch() returns false for unregistered actions,
// so the legacy inline chain stays as a fall-through during incremental migration — a command is
// migrated by deleting its inline branch and adding one on() registration.
//
// Handlers run on the game-loop (main) thread, same as the legacy chain. The dispatcher owns the
// onComplete/try-catch lifecycle; a handler just fills `response`.
class CommandRegistry {
public:
    using Handler = std::function<void(const APICommand& cmd, json& response)>;

    void on(std::string action, Handler handler) {
        m_handlers[std::move(action)] = std::move(handler);
    }
    bool has(const std::string& action) const { return m_handlers.count(action) != 0; }
    size_t size() const { return m_handlers.size(); }

    /// Run the handler for cmd.action, filling `response`. Returns false (untouched) if no
    /// handler is registered, so the caller can fall through to the legacy dispatch.
    bool dispatch(const APICommand& cmd, json& response) const {
        auto it = m_handlers.find(cmd.action);
        if (it == m_handlers.end()) return false;
        it->second(cmd, response);
        return true;
    }

private:
    std::unordered_map<std::string, Handler> m_handlers;
};

} // namespace Core
} // namespace Phyxel
