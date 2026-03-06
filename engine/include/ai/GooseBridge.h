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
    /// Path to the goosed binary (built from external/goose)
    std::string goosedPath = "goosed";

    /// Host the goose-server binds to
    std::string host = "127.0.0.1";

    /// Port the goose-server listens on
    uint16_t port = 3000;

    /// Secret key for API authentication
    std::string secretKey = "phyxel-engine";

    /// Working directory for goose sessions
    std::string workingDir = ".";

    /// Default LLM provider (e.g., "openai", "anthropic", "ollama")
    std::string defaultProvider = "openai";

    /// Default model name
    std::string defaultModel = "gpt-4o-mini";

    /// Enable TLS (goose-server uses self-signed certs by default)
    /// Note: Requires OpenSSL. Disabled by default for simpler setup.
    bool useTLS = false;

    /// Path to the Phyxel MCP extension script
    std::string mcpExtensionPath = "scripts/mcp/phyxel_extension.py";

    /// Max time to wait for goose-server to start (ms)
    uint32_t startupTimeoutMs = 15000;

    /// Base URL constructed from config
    std::string baseUrl() const {
        std::string scheme = useTLS ? "https" : "http";
        return scheme + "://" + host + ":" + std::to_string(port);
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
// SSE event types received from goose-server /reply endpoint
// ============================================================================

enum class SSEEventType {
    Message,
    Error,
    Finish,
    ModelChange,
    Notification,
    UpdateConversation,
    Ping,
    Unknown
};

struct SSEEvent {
    SSEEventType type = SSEEventType::Unknown;
    json data;

    /// Token usage info (present on Message and Finish events)
    struct TokenState {
        int64_t inputTokens = 0;
        int64_t outputTokens = 0;
        int64_t totalTokens = 0;
    };
    std::optional<TokenState> tokenState;
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

/// Called when an SSE event arrives from a /reply stream
using SSECallback = std::function<void(const SSEEvent& event)>;

/// Called when the agent finishes a reply cycle
using ReplyFinishCallback = std::function<void(const std::string& sessionId,
                                                const json& finalMessage)>;

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
    // Extension Management
    // ========================================================================

    /// Register the Phyxel MCP extension with the server's global config.
    /// This makes it available to all new sessions automatically.
    bool registerPhyxelExtension();

    /// Add a custom MCP extension to the server's global config.
    bool addGlobalExtension(const MCPExtensionConfig& ext);

    /// Add an MCP extension to a specific session.
    bool addSessionExtension(const std::string& sessionId,
                             const MCPExtensionConfig& ext);

    /// Remove an MCP extension from a specific session.
    bool removeSessionExtension(const std::string& sessionId,
                                const std::string& extensionName);

    // ========================================================================
    // Agent Session Management
    // ========================================================================

    /// Create a new agent session, optionally from a recipe file.
    /// Returns the session ID on success, empty string on failure.
    std::string createSession(const std::string& entityId,
                              const std::string& recipePath = "",
                              const std::vector<MCPExtensionConfig>& extraExtensions = {});

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
    /// The reply arrives via SSE streaming; parsed commands are pushed
    /// to the shared AICommandQueue.
    ///
    /// @param sessionId  Target session
    /// @param message    The user/game message (e.g., "A player approaches you")
    /// @param callback   Optional per-message callback for SSE events
    /// @return Future that resolves when the reply stream finishes
    std::future<bool> sendMessage(const std::string& sessionId,
                                  const std::string& message,
                                  SSECallback callback = nullptr);

    /// Send a structured game event to an agent session.
    /// Formats the event as a descriptive message for the AI.
    std::future<bool> sendGameEvent(const std::string& sessionId,
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
    // Recipe Management
    // ========================================================================

    /// Load a recipe from a YAML file and register it with goose-server.
    /// Returns the recipe ID on success.
    std::optional<std::string> loadRecipe(const std::string& yamlPath);

    /// List all registered recipes.
    std::vector<json> listRecipes();

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Set the LLM provider and model for future sessions.
    bool setProvider(const std::string& provider, const std::string& model);

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

    /// HTTP helper — performs a request to goose-server.
    /// Returns nullopt on connection failure.
    struct HttpResponse {
        int statusCode = 0;
        std::string body;
        bool ok() const { return statusCode >= 200 && statusCode < 300; }
    };

    std::optional<HttpResponse> httpGet(const std::string& path);
    std::optional<HttpResponse> httpPost(const std::string& path,
                                         const json& body = {});
    std::optional<HttpResponse> httpPut(const std::string& path,
                                        const json& body = {});
    std::optional<HttpResponse> httpDelete(const std::string& path);

    /// SSE stream reader — connects to /reply and processes events.
    void processSSEStream(const std::string& sessionId,
                          const json& requestBody,
                          SSECallback callback);

    /// Parse an SSE event line into an SSEEvent struct.
    SSEEvent parseSSEEvent(const std::string& data);

    /// Extract AI commands from a Message-type SSE event.
    /// Tool call results from the MCP extension are parsed into AICommands.
    void extractCommands(const json& message);

    /// Wait for the server to become healthy (polls /status).
    bool waitForServerReady(uint32_t timeoutMs);

    /// Monitor the sidecar process health in a background thread.
    void healthMonitorLoop();

    /// Start the goosed process.
    bool launchProcess();

    /// Kill the goosed process.
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

    // Async reply threads
    mutable std::mutex m_replyThreadsMutex;
    std::vector<std::future<void>> m_pendingReplies;

    // Callbacks
    ReplyFinishCallback m_replyFinishCallback;
    ServerStateCallback m_serverStateCallback;
    std::mutex m_callbackMutex;
};

} // namespace AI
} // namespace Phyxel
