#pragma once

#include <string>
#include <vector>
#include <future>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

using json = nlohmann::json;

// ============================================================================
// Configuration
// ============================================================================

struct LLMConfig {
    /// Provider: "anthropic", "openai", "ollama"
    std::string provider = "anthropic";

    /// Model name (empty = provider default)
    std::string model = "";

    /// API key (for cloud providers)
    std::string apiKey = "";

    /// Ollama host (for local models)
    std::string ollamaHost = "http://localhost:11434";

    /// Generation parameters
    int maxTokens       = 1024;
    float temperature   = 0.8f;
    int timeoutMs       = 30000;

    /// Default model per provider
    static std::string getDefaultModel(const std::string& provider);
};

// ============================================================================
// Message types
// ============================================================================

struct LLMMessage {
    std::string role;    // "system", "user", "assistant"
    std::string content;
};

struct LLMResponse {
    std::string content;        // AI response text
    std::string stopReason;     // "end_turn", "max_tokens", "stop", etc.
    int inputTokens  = 0;
    int outputTokens = 0;
    std::string error;          // Non-empty on failure

    bool ok() const { return error.empty(); }
};

// ============================================================================
// LLMClient — Direct HTTPS client for Claude, OpenAI, and Ollama
//
// Uses WinHTTP on Windows for zero-dependency HTTPS.
// Thread-safe: each call creates its own connection.
// ============================================================================

class LLMClient {
public:
    explicit LLMClient(const LLMConfig& config = {});
    ~LLMClient();

    // Non-copyable
    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;

    /// Synchronous completion — call from background thread
    LLMResponse complete(const std::vector<LLMMessage>& messages);

    /// Async completion — returns immediately
    std::future<LLMResponse> completeAsync(const std::vector<LLMMessage>& messages);

    /// Configuration
    void setConfig(const LLMConfig& config);
    const LLMConfig& getConfig() const { return m_config; }

    /// Returns true if the client has enough config to make API calls
    bool isConfigured() const;

    /// Cumulative token usage across all calls
    struct TokenUsage {
        int64_t totalInput  = 0;
        int64_t totalOutput = 0;
        int64_t totalCalls  = 0;
    };
    TokenUsage getTokenUsage() const;

private:
    // Provider-specific request builders
    LLMResponse callAnthropic(const std::vector<LLMMessage>& messages);
    LLMResponse callOpenAI(const std::vector<LLMMessage>& messages);
    LLMResponse callOllama(const std::vector<LLMMessage>& messages);

    // Low-level HTTPS POST (WinHTTP on Windows)
    struct HttpResult {
        int statusCode = 0;
        std::string body;
        std::string error;
        bool ok() const { return statusCode >= 200 && statusCode < 300 && error.empty(); }
    };

    HttpResult httpsPost(const std::string& host, int port,
                         const std::string& path,
                         const std::string& body,
                         const std::vector<std::pair<std::string, std::string>>& headers,
                         bool useTLS = true);

    // HTTP POST for local (non-TLS) endpoints like Ollama
    HttpResult httpPost(const std::string& host, int port,
                        const std::string& path,
                        const std::string& body,
                        const std::vector<std::pair<std::string, std::string>>& headers);

    LLMConfig m_config;

    // Token usage tracking
    mutable std::mutex m_usageMutex;
    TokenUsage m_usage;
};

} // namespace AI
} // namespace Phyxel
