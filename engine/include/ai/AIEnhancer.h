#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

namespace Phyxel {
namespace AI {

/**
 * Lightweight AI dialogue enhancer that calls Claude API via a Python script.
 * Spawns scripts/ai_enhance.py on a background thread and delivers the result
 * through a callback.  No Goose sidecar required.
 */
class AIEnhancer {
public:
    using ResultCallback = std::function<void(const std::string& nodeId,
                                              const std::string& enhancedText,
                                              const std::string& emotion)>;

    AIEnhancer(const std::string& apiKey,
               const std::string& model,
               const std::string& scriptPath);
    ~AIEnhancer();

    AIEnhancer(const AIEnhancer&) = delete;
    AIEnhancer& operator=(const AIEnhancer&) = delete;

    /// Request an AI-enhanced rephrase of a dialogue node.
    /// The callback is invoked from a background thread when the result arrives.
    void enhance(const std::string& nodeId,
                 const std::string& nodeText,
                 const std::string& speaker,
                 const std::string& emotion,
                 ResultCallback callback);

    bool isReady() const { return m_ready; }

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_scriptPath;
    std::atomic<bool> m_ready{false};

    // Guard to detach any lingering threads on destruction
    std::mutex m_threadMutex;
    std::vector<std::thread> m_threads;
    void cleanupThreads();
};

} // namespace AI
} // namespace Phyxel
