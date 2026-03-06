#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <variant>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace AI {

// ============================================================================
// AI Command Types
// Commands produced by the AI (via MCP tools) and consumed by the game loop.
// ============================================================================

struct MoveToCommand {
    std::string entityId;
    glm::vec3 target;
    float speed = 1.0f;
};

struct SayDialogCommand {
    std::string entityId;
    std::string text;
    std::string emotion;  // "neutral", "angry", "happy", "sad", etc.
};

struct PlayAnimationCommand {
    std::string entityId;
    std::string animationName;
    bool loop = false;
};

struct AttackCommand {
    std::string entityId;
    std::string targetId;
    std::string skillName;
};

struct EmoteCommand {
    std::string entityId;
    std::string emoteType;  // "wave", "bow", "shrug", etc.
};

struct SpawnEntityCommand {
    std::string templateName;
    glm::vec3 position;
    std::string assignedId;  // ID to give the spawned entity
};

struct SetQuestStateCommand {
    std::string questId;
    std::string state;
    std::string detail;
};

struct TriggerEventCommand {
    std::string eventName;
    std::string payload;  // JSON string with event-specific data
};

/// Union of all commands the AI can issue to the engine
using AICommand = std::variant<
    MoveToCommand,
    SayDialogCommand,
    PlayAnimationCommand,
    AttackCommand,
    EmoteCommand,
    SpawnEntityCommand,
    SetQuestStateCommand,
    TriggerEventCommand
>;

// ============================================================================
// AICommandQueue
// Thread-safe queue that decouples async AI decisions from the 60fps game loop.
//
// PRODUCER: PhyxelMCPExtension (Python, running in goose's MCP tool calls)
//           pushes commands via a shared-memory or HTTP callback.
//
// CONSUMER: Game loop calls drainCommands() once per frame and executes them.
// ============================================================================

class AICommandQueue {
public:
    /// Push a command (thread-safe, called from any thread)
    void push(AICommand cmd);

    /// Push multiple commands at once (single lock acquisition)
    void pushBatch(std::vector<AICommand> cmds);

    /// Drain all pending commands into the output vector (called from game loop)
    /// Returns the number of commands drained.
    size_t drainCommands(std::vector<AICommand>& out);

    /// Check if there are pending commands (lock-free approximate check)
    bool hasPending() const;

    /// Get approximate queue depth
    size_t approximateSize() const;

private:
    mutable std::mutex m_mutex;
    std::queue<AICommand> m_queue;
};

} // namespace AI
} // namespace Phyxel
