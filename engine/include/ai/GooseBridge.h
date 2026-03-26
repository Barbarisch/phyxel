#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <future>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "ai/AICommandQueue.h"

namespace Phyxel {
namespace AI {

using json = nlohmann::json;

// ============================================================================
// Configuration for the GooseBridge
// ============================================================================

struct GooseConfig {
    /// Path to the goose CLI binary
    std::string goosePath = "external/goose/bin/goose.exe";

    /// Path to the Python bridge script
    std::string bridgeScript = "scripts/goose_bridge.py";

    /// Host the bridge server binds to
    std::string host = "127.0.0.1";

    /// Port the goose web server listens on (managed by bridge)
    uint16_t goosePort = 3000;

    /// Port the Python bridge HTTP server listens on
    uint16_t bridgePort = 3001;

    /// Working directory for goose sessions
    std::string workingDir = ".";

    /// Default LLM provider (e.g., "anthropic", "openai", "ollama")
    std::string defaultProvider = "anthropic";

    /// Default model name
    std::string defaultModel = "claude-sonnet-4-20250514";

    /// Max time to wait for bridge+goose to start (ms)
    uint32_t startupTimeoutMs = 45000;

    /// Base URL for the Python bridge HTTP server
    std::string baseUrl() const {
        return "http://" + host + ":" + std::to_string(bridgePort);
    }
};

// ============================================================================
// Extension configuration for MCP tools
// ============================================================================

struct MCPExtensionConfig {
    std::string name;
    std::string type;  // "stdio", "sse", "builtin"

    // For stdio extensions
    std::string command;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> envVars;

    // For SSE extensions
    std::string uri;

    /// Convert to JSON for goose-server API
    json toJson() const;
};

// ============================================================================
// Chat response from the Goose bridge
// ============================================================================

struct ChatResponse {
    std::string response;              // AI's text response
    std::vector<json> toolCalls;       // Tool call requests from the AI
    std::string error;                 // Error message if any
    bool ok() const { return error.empty(); }
};

// ============================================================================
// Agent session handle — represents an active AI entity
// ============================================================================

struct AgentSession {
    std::string sessionId;
    std::string entityId;      // Phyxel entity this session is tied to
    std::string recipeName;    // Character recipe used
    bool isActive = false;

    /// Accumulated token usage
    int64_t totalInputTokens = 0;
    int64_t totalOutputTokens = 0;
};

// ============================================================================
// Callback types
// ============================================================================

/// Called when the agent finishes a reply cycle
using ReplyFinishCallback = std::function<void(const std::string& sessionId,
                                                const ChatResponse& response)>;

/// Called when the goose-server process state changes
using ServerStateCallback = std::function<void(bool isRunning)>;

// ============================================================================
// GooseBridge
//
// Manages the goose-server sidecar process and provides a C++ interface
// for creating agent sessions, sending messages, and receiving AI actions.
//
// Thread-safety: All public methods are thread-safe.
// ============================================================================

class GooseBridge {
public:
    explicit GooseBridge(const GooseConfig& config = {});
    ~GooseBridge();

    // Non-copyable, non-movable (contains mutex-based queue)
    GooseBridge(const GooseBridge&) = delete;
    GooseBridge& operator=(const GooseBridge&) = delete;
    GooseBridge(GooseBridge&&) = delete;
    GooseBridge& operator=(GooseBridge&&) = delete;

    // ========================================================================
    // Server Lifecycle
    // ========================================================================

    /// Start the goose-server sidecar process.
    /// Returns true if server started successfully and is responding.
    bool startServer();

    /// Stop the goose-server sidecar process.
    void stopServer();

    /// Check if the goose-server is running and healthy.
    bool isServerRunning() const;

    /// Set callback for server state changes.
    void setServerStateCallback(ServerStateCallback callback);

    // ========================================================================
    // Extension Management (reserved for future use)
    // ========================================================================

    /// Register the Phyxel MCP extension with the server.
    bool registerPhyxelExtension();

    // ========================================================================
    // Agent Session Management
    // ========================================================================

    /// Create a new agent session.
    /// Returns the session ID on success, empty string on failure.
    std::string createSession(const std::string& entityId);

    /// Resume a previously created session.
    bool resumeSession(const std::string& sessionId);

    /// Stop and destroy an agent session.
    bool destroySession(const std::string& sessionId);

    /// Get session info. Returns nullopt if not found.
    std::optional<AgentSession> getSession(const std::string& sessionId) const;

    /// Get all active sessions.
    std::vector<AgentSession> getActiveSessions() const;

    /// Get session ID for a given entity, if one exists.
    std::optional<std::string> getSessionForEntity(const std::string& entityId) const;

    // ========================================================================
    // Communication
    // ========================================================================

    /// Send a message to an agent session asynchronously.
    /// The reply is a complete ChatResponse from the bridge.
    ///
    /// @param sessionId  Target session
    /// @param message    The user/game message (e.g., "A player approaches you")
    /// @return Future that resolves to the complete ChatResponse
    std::future<ChatResponse> sendMessage(const std::string& sessionId,
                                          const std::string& message);

    /// Send a structured game event to an agent session.
    /// Formats the event as a descriptive message for the AI.
    std::future<ChatResponse> sendGameEvent(const std::string& sessionId,
                                            const std::string& eventType,
                                            const json& eventData);

    /// Set the default callback for when any reply finishes.
    void setReplyFinishCallback(ReplyFinishCallback callback);

    // ========================================================================
    // Command Queue Access
    // ========================================================================

    /// Get the shared command queue.
    /// Game loop should call queue.drainCommands() each frame.
    AICommandQueue& getCommandQueue() { return m_commandQueue; }
    const AICommandQueue& getCommandQueue() const { return m_commandQueue; }

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Get the current configuration.
    const GooseConfig& getConfig() const { return m_config; }

    // ========================================================================
    // Diagnostics
    // ========================================================================

    /// Get aggregate token usage across all sessions.
    struct TokenUsage {
        int64_t totalInputTokens = 0;
        int64_t totalOutputTokens = 0;
        int64_t totalTokens() const { return totalInputTokens + totalOutputTokens; }
    };
    TokenUsage getTotalTokenUsage() const;

    /// Get the number of active sessions.
    size_t getActiveSessionCount() const;

private:
    // ========================================================================
    // Internal Implementation
    // ========================================================================

    /// HTTP helper — performs a request to the bridge server.
    /// Returns nullopt on connection failure.
    struct HttpResponse {
        int statusCode = 0;
        std::string body;
        bool ok() const { return statusCode >= 200 && statusCode < 300; }
    };

    std::optional<HttpResponse> httpGet(const std::string& path);
    std::optional<HttpResponse> httpPost(const std::string& path,
                                         const json& body = {});

    /// Extract AI commands from tool_calls in a ChatResponse.
    void extractCommands(const std::vector<json>& toolCalls);

    /// Wait for the bridge server to become healthy (polls /status).
    bool waitForServerReady(uint32_t timeoutMs);

    /// Monitor the bridge process health in a background thread.
    void healthMonitorLoop();

    /// Start the Python bridge process (which also starts goose).
    bool launchProcess();

    /// Kill the bridge process (and goose with it).
    void killProcess();

    // ========================================================================
    // State
    // ========================================================================

    GooseConfig m_config;
    AICommandQueue m_commandQueue;

    // Server process management
    std::atomic<bool> m_serverRunning{false};
    std::thread m_healthThread;
    std::atomic<bool> m_healthThreadRunning{false};

    // Platform-specific process handle
#ifdef _WIN32
    void* m_processHandle = nullptr;  // HANDLE from CreateProcess
#else
    pid_t m_processPid = -1;
#endif

    // Session tracking
    mutable std::mutex m_sessionsMutex;
    std::unordered_map<std::string, AgentSession> m_sessions;         // sessionId → session
    std::unordered_map<std::string, std::string> m_entityToSession;   // entityId → sessionId

    // Callbacks
    ReplyFinishCallback m_replyFinishCallback;
    ServerStateCallback m_serverStateCallback;
    std::mutex m_callbackMutex;
};

} // namespace AI
} // namespace Phyxel
