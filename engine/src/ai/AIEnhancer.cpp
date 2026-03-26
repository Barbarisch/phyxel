#include "ai/AIEnhancer.h"
#include "utils/Logger.h"
#include <array>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace Phyxel {
namespace AI {

AIEnhancer::AIEnhancer(const std::string& apiKey,
                       const std::string& model,
                       const std::string& scriptPath)
    : m_apiKey(apiKey)
    , m_model(model)
    , m_scriptPath(scriptPath)
{
    m_ready = !m_apiKey.empty() && !m_scriptPath.empty();
    if (m_ready) {
        LOG_INFO("AIEnhancer", "Ready (model={}, script={})", m_model, m_scriptPath);
    } else {
        LOG_WARN("AIEnhancer", "Not ready — apiKey empty={}, scriptPath empty={}",
                 m_apiKey.empty(), m_scriptPath.empty());
    }
}

AIEnhancer::~AIEnhancer() {
    // Wait for any running threads
    std::lock_guard<std::mutex> lock(m_threadMutex);
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
}

void AIEnhancer::cleanupThreads() {
    // Remove finished threads (called under lock)
    m_threads.erase(
        std::remove_if(m_threads.begin(), m_threads.end(),
                       [](std::thread& t) {
                           if (!t.joinable()) return true;
                           return false;
                       }),
        m_threads.end());
}

/// Escape a string for safe use as a command-line argument (Windows shell).
static std::string shellEscape(const std::string& s) {
    // For _popen on Windows we wrap in double-quotes and escape inner quotes
    std::string result;
    result.reserve(s.size() + 10);
    result += '"';
    for (char c : s) {
        if (c == '"')  result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    result += '"';
    return result;
}

void AIEnhancer::enhance(const std::string& nodeId,
                         const std::string& nodeText,
                         const std::string& speaker,
                         const std::string& emotion,
                         ResultCallback callback) {
    if (!m_ready || !callback) return;

    // Build system and user prompts
    std::string systemPrompt = "You are " + speaker +
        ", a character in a fantasy village. Stay in character. "
        "Respond naturally and expressively.";
    std::string userPrompt = "Rephrase this dialogue line in your own words, staying in character as " +
        speaker + ". Keep it roughly the same length. Only output the rephrased line, nothing else.\n\n"
        "Original: " + nodeText;

    // Capture what we need for the thread
    std::string apiKey    = m_apiKey;
    std::string model     = m_model;
    std::string script    = m_scriptPath;
    std::string nid       = nodeId;
    std::string emo       = emotion;

    std::lock_guard<std::mutex> lock(m_threadMutex);
    cleanupThreads();

    m_threads.emplace_back([apiKey, model, script, systemPrompt, userPrompt,
                            nid, emo, callback = std::move(callback)]() {
        // Write prompts to a temp file as JSON, then pipe to Python via stdin
        auto tempPath = std::filesystem::temp_directory_path() / ("phyxel_ai_" + nid + ".json");
        {
            std::ofstream f(tempPath);
            // Manual JSON construction to avoid dependency on nlohmann/json here
            f << "{\"system\":\"";
            for (char c : systemPrompt) {
                if (c == '"') f << "\\\"";
                else if (c == '\\') f << "\\\\";
                else if (c == '\n') f << "\\n";
                else if (c == '\r') f << "\\r";
                else f << c;
            }
            f << "\",\"user\":\"";
            for (char c : userPrompt) {
                if (c == '"') f << "\\\"";
                else if (c == '\\') f << "\\\\";
                else if (c == '\n') f << "\\n";
                else if (c == '\r') f << "\\r";
                else f << c;
            }
            f << "\"}";
        }

        // Build command:  python <script> <key> <model> < tempfile
        std::string cmd = "python " + shellEscape(script) + " "
                        + shellEscape(apiKey) + " "
                        + shellEscape(model)  + " < "
                        + shellEscape(tempPath.string());

        LOG_INFO("AIEnhancer", "Calling AI for node '{}' ...", nid);

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOG_WARN("AIEnhancer", "Failed to spawn ai_enhance.py");
            std::filesystem::remove(tempPath);
            return;
        }

        std::string result;
        std::array<char, 256> buf;
        while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
            result += buf.data();
        }
        int exitCode = pclose(pipe);
        std::filesystem::remove(tempPath);

        if (exitCode != 0 || result.empty()) {
            LOG_WARN("AIEnhancer", "ai_enhance.py failed (exit={})", exitCode);
            return;
        }

        // Trim trailing whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
            result.pop_back();

        LOG_INFO("AIEnhancer", "AI response for '{}': {}", nid, result);
        callback(nid, result, emo);
    });
}

} // namespace AI
} // namespace Phyxel
