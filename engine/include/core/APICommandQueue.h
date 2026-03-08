#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

// ============================================================================
// APICommand
//
// A generic command from the HTTP API server to the game loop.
// The game loop processes these once per frame.
//
// Unlike AICommand (which is NPC-specific), APICommand represents
// any engine operation an external agent might request:
//   - Spawning entities
//   - Placing/removing voxels
//   - Querying world state
//   - Modifying entity properties
//   - Running Python scripts
//   - Controlling the camera
//
// The handler returns a JSON response that gets sent back to the HTTP caller.
// ============================================================================

struct APICommand {
    std::string action;         // e.g., "spawn_entity", "place_voxel", "query_state"
    json params;                // Action-specific parameters
    uint64_t requestId = 0;     // Unique request ID for response correlation

    /// Promise-like callback: the game loop calls this with the result.
    /// The HTTP handler blocks waiting for this to be called.
    std::function<void(json response)> onComplete;
};

// ============================================================================
// APICommandQueue
//
// Thread-safe queue that decouples the HTTP API server (background thread)
// from the game loop (main thread).
//
// Pattern:
//   1. HTTP handler receives request, creates APICommand with a
//      std::promise-based onComplete callback
//   2. HTTP handler pushes command and waits on the future
//   3. Game loop calls processCommands() once per frame
//   4. processCommands() executes the handler and calls onComplete
//   5. HTTP handler receives the response and returns it to the caller
// ============================================================================

class APICommandQueue {
public:
    APICommandQueue() = default;
    ~APICommandQueue() = default;

    // Non-copyable
    APICommandQueue(const APICommandQueue&) = delete;
    APICommandQueue& operator=(const APICommandQueue&) = delete;

    /// Push a command (thread-safe, called from HTTP server thread).
    void push(APICommand cmd);

    /// Drain all pending commands into the output vector (called from game loop).
    /// Returns the number of commands drained.
    size_t drainCommands(std::vector<APICommand>& out);

    /// Check if there are pending commands.
    bool hasPending() const;

    /// Get approximate queue depth.
    size_t approximateSize() const;

private:
    mutable std::mutex m_mutex;
    std::queue<APICommand> m_queue;
};

} // namespace Core
} // namespace Phyxel
