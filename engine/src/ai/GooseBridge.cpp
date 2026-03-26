#include "ai/GooseBridge.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <filesystem>

// Platform-specific includes for process management
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <signal.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

// For HTTP client (using cpp-httplib, no SSL)
#include <httplib.h>

#include "utils/Logger.h"

namespace Phyxel {
namespace AI {

// ============================================================================
// MCPExtensionConfig
// ============================================================================

json MCPExtensionConfig::toJson() const {
    json j;
    j["name"] = name;

    json config;
    if (type == "stdio") {
        config["type"] = "Stdio";
        config["cmd"] = command;
        json argsJson = json::array();
        for (const auto& a : args) argsJson.push_back(a);
        config["args"] = argsJson;
        if (!envVars.empty()) {
            json env;
            for (const auto& [k, v] : envVars) env[k] = v;
            config["envs"] = env;
        }
    } else if (type == "sse") {
        config["type"] = "Sse";
        config["uri"] = uri;
    } else if (type == "builtin") {
        config["type"] = "Builtin";
        config["name"] = name;
    }

    j["config"] = config;
    j["enabled"] = true;
    return j;
}

// ============================================================================
// GooseBridge — Construction / Destruction
// ============================================================================

GooseBridge::GooseBridge(const GooseConfig& config)
    : m_config(config)
{
    LOG_INFO("AI", "GooseBridge created (bridge at {})", m_config.baseUrl());
}

GooseBridge::~GooseBridge() {
    stopServer();
}

// ============================================================================
// Server Lifecycle
// ============================================================================

bool GooseBridge::startServer() {
    if (m_serverRunning) {
        LOG_WARN("AI", "GooseBridge: server already running");
        return true;
    }

    LOG_INFO("AI", "GooseBridge: starting Python bridge + goose web...");

    if (!launchProcess()) {
        LOG_ERROR("AI", "GooseBridge: failed to launch bridge process");
        return false;
    }

    if (!waitForServerReady(m_config.startupTimeoutMs)) {
        LOG_ERROR("AI", "GooseBridge: bridge did not become healthy within {}ms",
                  m_config.startupTimeoutMs);
        killProcess();
        return false;
    }

    m_serverRunning = true;
    LOG_INFO("AI", "GooseBridge: bridge is ready at {}", m_config.baseUrl());

    // Start health monitoring
    m_healthThreadRunning = true;
    m_healthThread = std::thread(&GooseBridge::healthMonitorLoop, this);

    // Notify callback
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_serverStateCallback) m_serverStateCallback(true);
    }

    return true;
}

void GooseBridge::stopServer() {
    if (!m_serverRunning) return;

    LOG_INFO("AI", "GooseBridge: stopping...");

    // Stop health monitor
    m_healthThreadRunning = false;
    if (m_healthThread.joinable()) {
        m_healthThread.join();
    }

    // Destroy all active sessions via bridge
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto& [id, session] : m_sessions) {
            if (session.isActive) {
                json body;
                body["session_id"] = id;
                httpPost("/session/destroy", body);
                session.isActive = false;
            }
        }
        m_sessions.clear();
        m_entityToSession.clear();
    }

    killProcess();
    m_serverRunning = false;

    // Notify callback
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_serverStateCallback) m_serverStateCallback(false);
    }

    LOG_INFO("AI", "GooseBridge: stopped");
}

bool GooseBridge::isServerRunning() const {
    return m_serverRunning;
}

void GooseBridge::setServerStateCallback(ServerStateCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_serverStateCallback = std::move(callback);
}

// ============================================================================
// Extension Management (stub — reserved for future use)
// ============================================================================

bool GooseBridge::registerPhyxelExtension() {
    // In the bridge architecture, extensions are configured via Goose's config.yaml
    LOG_INFO("AI", "GooseBridge: Phyxel extension registration is handled via Goose config");
    return true;
}

// ============================================================================
// Agent Session Management
// ============================================================================

std::string GooseBridge::createSession(const std::string& entityId)
{
    auto resp = httpPost("/session/create");
    if (!resp || !resp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to create session for entity '{}'", entityId);
        return "";
    }

    auto respJson = json::parse(resp->body, nullptr, false);
    if (respJson.is_discarded() || !respJson.contains("session_id")) {
        LOG_ERROR("AI", "GooseBridge: invalid response from /session/create");
        return "";
    }

    std::string sessionId = respJson["session_id"].get<std::string>();

    // Track the session
    AgentSession session;
    session.sessionId = sessionId;
    session.entityId = entityId;
    session.isActive = true;

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions[sessionId] = session;
        m_entityToSession[entityId] = sessionId;
    }

    LOG_INFO("AI", "GooseBridge: created session {} for entity '{}'", sessionId, entityId);
    return sessionId;
}

bool GooseBridge::resumeSession(const std::string& sessionId) {
    // The bridge keeps sessions alive in Goose — just re-mark as active
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    if (m_sessions.count(sessionId)) {
        m_sessions[sessionId].isActive = true;
        return true;
    }
    return false;
}

bool GooseBridge::destroySession(const std::string& sessionId) {
    json body;
    body["session_id"] = sessionId;
    auto resp = httpPost("/session/destroy", body);

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(sessionId);
        if (it != m_sessions.end()) {
            m_entityToSession.erase(it->second.entityId);
            m_sessions.erase(it);
        }
    }

    LOG_INFO("AI", "GooseBridge: destroyed session {}", sessionId);
    return resp && resp->ok();
}

std::optional<AgentSession> GooseBridge::getSession(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return std::nullopt;
    return it->second;
}

std::vector<AgentSession> GooseBridge::getActiveSessions() const {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    std::vector<AgentSession> result;
    result.reserve(m_sessions.size());
    for (const auto& [id, session] : m_sessions) {
        if (session.isActive) result.push_back(session);
    }
    return result;
}

std::optional<std::string> GooseBridge::getSessionForEntity(const std::string& entityId) const {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_entityToSession.find(entityId);
    if (it == m_entityToSession.end()) return std::nullopt;
    return it->second;
}

// ============================================================================
// Communication
// ============================================================================

std::future<ChatResponse> GooseBridge::sendMessage(
    const std::string& sessionId,
    const std::string& message)
{
    return std::async(std::launch::async, [this, sessionId, message]() -> ChatResponse {
        try {
            json body;
            body["session_id"] = sessionId;
            body["message"] = message;

            auto resp = httpPost("/chat", body);
            if (!resp) {
                return ChatResponse{"", {}, "Connection to bridge failed"};
            }

            auto respJson = json::parse(resp->body, nullptr, false);
            if (respJson.is_discarded()) {
                return ChatResponse{"", {}, "Invalid JSON response"};
            }

            ChatResponse chatResp;
            chatResp.response = respJson.value("response", "");
            chatResp.error = respJson.value("error", "");

            if (respJson.contains("tool_calls") && respJson["tool_calls"].is_array()) {
                chatResp.toolCalls = respJson["tool_calls"].get<std::vector<json>>();
                // Extract engine commands from tool calls
                extractCommands(chatResp.toolCalls);
            }

            // Notify finish callback
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                if (m_replyFinishCallback) {
                    m_replyFinishCallback(sessionId, chatResp);
                }
            }

            return chatResp;
        } catch (const std::exception& e) {
            LOG_ERROR("AI", "GooseBridge: error sending message to session {}: {}",
                      sessionId, e.what());
            return ChatResponse{"", {}, e.what()};
        }
    });
}

std::future<ChatResponse> GooseBridge::sendGameEvent(
    const std::string& sessionId,
    const std::string& eventType,
    const json& eventData)
{
    std::ostringstream msg;
    msg << "[Game Event: " << eventType << "]\n";
    msg << eventData.dump(2);

    return sendMessage(sessionId, msg.str());
}

void GooseBridge::setReplyFinishCallback(ReplyFinishCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_replyFinishCallback = std::move(callback);
}

// ============================================================================
// Diagnostics
// ============================================================================

GooseBridge::TokenUsage GooseBridge::getTotalTokenUsage() const {
    TokenUsage usage;
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (const auto& [id, session] : m_sessions) {
        usage.totalInputTokens += session.totalInputTokens;
        usage.totalOutputTokens += session.totalOutputTokens;
    }
    return usage;
}

size_t GooseBridge::getActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    return std::count_if(m_sessions.begin(), m_sessions.end(),
        [](const auto& pair) { return pair.second.isActive; });
}

// ============================================================================
// Internal: HTTP Client (talks to the Python bridge on bridgePort)
// ============================================================================

std::optional<GooseBridge::HttpResponse> GooseBridge::httpGet(const std::string& path) {
    try {
        httplib::Client cli(m_config.host, m_config.bridgePort);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(30);

        auto result = cli.Get(path);
        if (!result) {
            LOG_ERROR("AI", "GooseBridge HTTP GET {} failed: connection error", path);
            return std::nullopt;
        }

        return HttpResponse{result->status, result->body};
    } catch (const std::exception& e) {
        LOG_ERROR("AI", "GooseBridge HTTP GET {} exception: {}", path, e.what());
        return std::nullopt;
    }
}

std::optional<GooseBridge::HttpResponse> GooseBridge::httpPost(
    const std::string& path, const json& body)
{
    try {
        httplib::Client cli(m_config.host, m_config.bridgePort);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(120);  // AI replies can take a while

        std::string bodyStr = body.dump();
        auto result = cli.Post(path, bodyStr, "application/json");
        if (!result) {
            LOG_ERROR("AI", "GooseBridge HTTP POST {} failed: connection error", path);
            return std::nullopt;
        }

        return HttpResponse{result->status, result->body};
    } catch (const std::exception& e) {
        LOG_ERROR("AI", "GooseBridge HTTP POST {} exception: {}", path, e.what());
        return std::nullopt;
    }
}

// ============================================================================
// Internal: Command Extraction from Tool Calls
// ============================================================================

void GooseBridge::extractCommands(const std::vector<json>& toolCalls) {
    for (const auto& call : toolCalls) {
        std::string toolName = call.value("tool_name", "");
        json input = call.value("arguments", json::object());

        if (toolName == "move_to") {
            MoveToCommand cmd;
            cmd.entityId = input.value("entity_id", "");
            cmd.target = glm::vec3(
                input.value("x", 0.0f),
                input.value("y", 0.0f),
                input.value("z", 0.0f)
            );
            cmd.speed = input.value("speed", 1.0f);
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "say_dialog") {
            SayDialogCommand cmd;
            cmd.entityId = input.value("entity_id", "");
            cmd.text = input.value("text", "");
            cmd.emotion = input.value("emotion", "neutral");
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "play_animation") {
            PlayAnimationCommand cmd;
            cmd.entityId = input.value("entity_id", "");
            cmd.animationName = input.value("animation_name", "");
            cmd.loop = input.value("loop", false);
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "attack") {
            AttackCommand cmd;
            cmd.entityId = input.value("entity_id", "");
            cmd.targetId = input.value("target_id", "");
            cmd.skillName = input.value("skill_name", "basic_attack");
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "emote") {
            EmoteCommand cmd;
            cmd.entityId = input.value("entity_id", "");
            cmd.emoteType = input.value("emote_type", "idle");
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "spawn_entity") {
            SpawnEntityCommand cmd;
            cmd.templateName = input.value("template", "");
            cmd.position = glm::vec3(
                input.value("x", 0.0f),
                input.value("y", 0.0f),
                input.value("z", 0.0f)
            );
            cmd.assignedId = input.value("entity_id", "");
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "set_quest_state") {
            SetQuestStateCommand cmd;
            cmd.questId = input.value("quest_id", "");
            cmd.state = input.value("state", "");
            cmd.detail = input.value("detail", "");
            m_commandQueue.push(std::move(cmd));
        }
        else if (toolName == "trigger_event") {
            TriggerEventCommand cmd;
            cmd.eventName = input.value("event_name", "");
            cmd.payload = input.value("payload", json::object()).dump();
            m_commandQueue.push(std::move(cmd));
        }
    }
}

// ============================================================================
// Internal: Server Health
// ============================================================================

bool GooseBridge::waitForServerReady(uint32_t timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();

        if (elapsed >= timeoutMs) return false;

        auto resp = httpGet("/status");
        if (resp && resp->ok()) {
            auto data = json::parse(resp->body, nullptr, false);
            if (!data.is_discarded() && data.value("goose_alive", false)) {
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void GooseBridge::healthMonitorLoop() {
    while (m_healthThreadRunning) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!m_healthThreadRunning) break;

        auto resp = httpGet("/status");
        bool healthy = resp && resp->ok();
        if (healthy) {
            auto data = json::parse(resp->body, nullptr, false);
            healthy = !data.is_discarded() && data.value("goose_alive", false);
        }

        if (!healthy && m_serverRunning) {
            LOG_WARN("AI", "GooseBridge: bridge health check failed, attempting restart...");
            killProcess();

            if (launchProcess() && waitForServerReady(m_config.startupTimeoutMs)) {
                LOG_INFO("AI", "GooseBridge: bridge restarted successfully");
            } else {
                LOG_ERROR("AI", "GooseBridge: failed to restart bridge");
                m_serverRunning = false;

                std::lock_guard<std::mutex> lock(m_callbackMutex);
                if (m_serverStateCallback) m_serverStateCallback(false);
            }
        }
    }
}

// ============================================================================
// Internal: Process Management
// ============================================================================

bool GooseBridge::launchProcess() {
    // Resolve paths
    std::string goosePath = m_config.goosePath;
    std::string bridgeScript = m_config.bridgeScript;

    // Make paths absolute if relative
    if (!std::filesystem::path(goosePath).is_absolute()) {
        goosePath = (std::filesystem::current_path() / goosePath).string();
    }
    if (!std::filesystem::path(bridgeScript).is_absolute()) {
        bridgeScript = (std::filesystem::current_path() / bridgeScript).string();
    }

    if (!std::filesystem::exists(goosePath)) {
        LOG_ERROR("AI", "GooseBridge: goose binary not found at: {}", goosePath);
        return false;
    }
    if (!std::filesystem::exists(bridgeScript)) {
        LOG_ERROR("AI", "GooseBridge: bridge script not found at: {}", bridgeScript);
        return false;
    }

#ifdef _WIN32
    // Find Python — prefer .venv in the engine or working directory
    std::string pythonExe = "python";
    auto cwd = std::filesystem::current_path();
    auto venvPython = cwd / ".venv" / "Scripts" / "python.exe";
    if (std::filesystem::exists(venvPython)) {
        pythonExe = "\"" + venvPython.string() + "\"";
        LOG_INFO("AI", "GooseBridge: using venv Python at {}", venvPython.string());
    }

    // Build command line: python goose_bridge.py --goose-path <path> --goose-port <port> --bridge-port <port>
    std::string cmdLine = pythonExe + " \"" + bridgeScript + "\""
        + " --goose-path \"" + goosePath + "\""
        + " --goose-port " + std::to_string(m_config.goosePort)
        + " --bridge-port " + std::to_string(m_config.bridgePort);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Pass ANTHROPIC_API_KEY from PHYXEL_AI_API_KEY if not already set
    if (const char* phyxelKey = std::getenv("PHYXEL_AI_API_KEY")) {
        if (!std::getenv("ANTHROPIC_API_KEY")) {
            SetEnvironmentVariableA("ANTHROPIC_API_KEY", phyxelKey);
        }
    }

    BOOL success = CreateProcessA(
        nullptr,
        const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        m_config.workingDir.c_str(),
        &si, &pi
    );

    if (!success) {
        LOG_ERROR("AI", "GooseBridge: CreateProcess failed with error {}", GetLastError());
        return false;
    }

    m_processHandle = pi.hProcess;
    CloseHandle(pi.hThread);

    LOG_INFO("AI", "GooseBridge: launched bridge process (PID: {})", pi.dwProcessId);
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("AI", "GooseBridge: fork() failed");
        return false;
    }

    if (pid == 0) {
        // Child process — pass ANTHROPIC_API_KEY from PHYXEL_AI_API_KEY if needed
        if (const char* phyxelKey = std::getenv("PHYXEL_AI_API_KEY")) {
            if (!std::getenv("ANTHROPIC_API_KEY")) {
                setenv("ANTHROPIC_API_KEY", phyxelKey, 1);
            }
        }

        execlp("python3", "python3",
               bridgeScript.c_str(),
               "--goose-path", goosePath.c_str(),
               "--goose-port", std::to_string(m_config.goosePort).c_str(),
               "--bridge-port", std::to_string(m_config.bridgePort).c_str(),
               nullptr);
        _exit(1);  // exec failed
    }

    m_processPid = pid;
    LOG_INFO("AI", "GooseBridge: launched bridge process (PID: {})", pid);
    return true;
#endif
}

void GooseBridge::killProcess() {
#ifdef _WIN32
    if (m_processHandle) {
        TerminateProcess(m_processHandle, 0);
        WaitForSingleObject(m_processHandle, 5000);
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
#else
    if (m_processPid > 0) {
        kill(m_processPid, SIGTERM);
        int status;
        int waited = waitpid(m_processPid, &status, WNOHANG);
        if (waited == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            waited = waitpid(m_processPid, &status, WNOHANG);
            if (waited == 0) {
                kill(m_processPid, SIGKILL);
                waitpid(m_processPid, &status, 0);
            }
        }
        m_processPid = -1;
    }
#endif
}

} // namespace AI
} // namespace Phyxel
