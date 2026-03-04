#include "ai/GooseBridge.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <fstream>
#include <algorithm>

// Platform-specific includes for process management
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #include <signal.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

// For HTTP client (using cpp-httplib, no SSL)
#include <httplib.h>

#include "utils/Logger.h"

namespace VulkanCube {
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
    LOG_INFO("AI", "GooseBridge created with server at {}", m_config.baseUrl());
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

    LOG_INFO("AI", "GooseBridge: starting goose-server...");

    if (!launchProcess()) {
        LOG_ERROR("AI", "GooseBridge: failed to launch goosed process");
        return false;
    }

    if (!waitForServerReady(m_config.startupTimeoutMs)) {
        LOG_ERROR("AI", "GooseBridge: goosed did not become healthy within {}ms",
                  m_config.startupTimeoutMs);
        killProcess();
        return false;
    }

    m_serverRunning = true;
    LOG_INFO("AI", "GooseBridge: goose-server is ready at {}", m_config.baseUrl());

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

    LOG_INFO("AI", "GooseBridge: stopping goose-server...");

    // Stop health monitor
    m_healthThreadRunning = false;
    if (m_healthThread.joinable()) {
        m_healthThread.join();
    }

    // Destroy all active sessions
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto& [id, session] : m_sessions) {
            if (session.isActive) {
                json body;
                body["session_id"] = id;
                httpPost("/agent/stop", body);
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

    LOG_INFO("AI", "GooseBridge: server stopped");
}

bool GooseBridge::isServerRunning() const {
    return m_serverRunning;
}

void GooseBridge::setServerStateCallback(ServerStateCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_serverStateCallback = std::move(callback);
}

// ============================================================================
// Extension Management
// ============================================================================

bool GooseBridge::registerPhyxelExtension() {
    MCPExtensionConfig ext;
    ext.name = "phyxel";
    ext.type = "stdio";
    ext.command = "python";
    ext.args = {m_config.mcpExtensionPath};
    ext.envVars["PHYXEL_ENGINE_HOST"] = m_config.host;

    return addGlobalExtension(ext);
}

bool GooseBridge::addGlobalExtension(const MCPExtensionConfig& ext) {
    auto resp = httpPost("/config/extensions", ext.toJson());
    if (!resp || !resp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to register global extension '{}'", ext.name);
        return false;
    }
    LOG_INFO("AI", "GooseBridge: registered global extension '{}'", ext.name);
    return true;
}

bool GooseBridge::addSessionExtension(const std::string& sessionId,
                                       const MCPExtensionConfig& ext) {
    json body;
    body["session_id"] = sessionId;
    body["config"] = ext.toJson()["config"];

    auto resp = httpPost("/agent/add_extension", body);
    if (!resp || !resp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to add extension '{}' to session {}",
                  ext.name, sessionId);
        return false;
    }
    return true;
}

bool GooseBridge::removeSessionExtension(const std::string& sessionId,
                                          const std::string& extensionName) {
    json body;
    body["session_id"] = sessionId;
    body["name"] = extensionName;

    auto resp = httpPost("/agent/remove_extension", body);
    return resp && resp->ok();
}

// ============================================================================
// Agent Session Management
// ============================================================================

std::string GooseBridge::createSession(
    const std::string& entityId,
    const std::string& recipePath,
    const std::vector<MCPExtensionConfig>& extraExtensions)
{
    json body;
    body["working_dir"] = m_config.workingDir;

    // Load recipe from YAML file if provided
    if (!recipePath.empty()) {
        std::ifstream file(recipePath);
        if (file.is_open()) {
            // Read YAML and let goose-server parse it
            std::string yamlContent((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
            // First parse the YAML via the recipes API
            json parseBody;
            parseBody["content"] = yamlContent;
            auto parseResp = httpPost("/recipes/parse", parseBody);
            if (parseResp && parseResp->ok()) {
                auto parsed = json::parse(parseResp->body, nullptr, false);
                if (!parsed.is_discarded() && parsed.contains("recipe")) {
                    body["recipe"] = parsed["recipe"];
                }
            }
        } else {
            LOG_WARN("AI", "GooseBridge: could not open recipe file: {}", recipePath);
        }
    }

    // Add extension overrides if any
    if (!extraExtensions.empty()) {
        json overrides = json::array();
        for (const auto& ext : extraExtensions) {
            overrides.push_back(ext.toJson());
        }
        body["extension_overrides"] = overrides;
    }

    auto resp = httpPost("/agent/start", body);
    if (!resp || !resp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to create session for entity '{}'", entityId);
        return "";
    }

    auto respJson = json::parse(resp->body, nullptr, false);
    if (respJson.is_discarded() || !respJson.contains("session_id")) {
        LOG_ERROR("AI", "GooseBridge: invalid response from /agent/start");
        return "";
    }

    std::string sessionId = respJson["session_id"].get<std::string>();

    // Track the session
    AgentSession session;
    session.sessionId = sessionId;
    session.entityId = entityId;
    session.isActive = true;
    session.recipeName = recipePath;

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions[sessionId] = session;
        m_entityToSession[entityId] = sessionId;
    }

    LOG_INFO("AI", "GooseBridge: created session {} for entity '{}'", sessionId, entityId);
    return sessionId;
}

bool GooseBridge::resumeSession(const std::string& sessionId) {
    json body;
    body["session_id"] = sessionId;
    body["load_model_and_extensions"] = true;

    auto resp = httpPost("/agent/resume", body);
    if (!resp || !resp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to resume session {}", sessionId);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        if (m_sessions.count(sessionId)) {
            m_sessions[sessionId].isActive = true;
        }
    }

    LOG_INFO("AI", "GooseBridge: resumed session {}", sessionId);
    return true;
}

bool GooseBridge::destroySession(const std::string& sessionId) {
    json body;
    body["session_id"] = sessionId;

    auto resp = httpPost("/agent/stop", body);

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

std::future<bool> GooseBridge::sendMessage(
    const std::string& sessionId,
    const std::string& message,
    SSECallback callback)
{
    json body;
    body["session_id"] = sessionId;

    // Build the user message in Goose's Message format
    json userMsg;
    userMsg["role"] = "user";
    json content = json::array();
    json textPart;
    textPart["type"] = "text";
    textPart["text"] = message;
    content.push_back(textPart);
    userMsg["content"] = content;
    body["user_message"] = userMsg;

    return std::async(std::launch::async, [this, sessionId, body, callback]() -> bool {
        try {
            processSSEStream(sessionId, body, callback);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("AI", "GooseBridge: error in reply stream for session {}: {}",
                      sessionId, e.what());
            return false;
        }
    });
}

std::future<bool> GooseBridge::sendGameEvent(
    const std::string& sessionId,
    const std::string& eventType,
    const json& eventData)
{
    // Format game events into natural language for the AI
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
// Recipe Management
// ============================================================================

std::optional<std::string> GooseBridge::loadRecipe(const std::string& yamlPath) {
    std::ifstream file(yamlPath);
    if (!file.is_open()) {
        LOG_ERROR("AI", "GooseBridge: cannot open recipe file: {}", yamlPath);
        return std::nullopt;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Parse YAML via goose-server
    json parseBody;
    parseBody["content"] = content;
    auto parseResp = httpPost("/recipes/parse", parseBody);
    if (!parseResp || !parseResp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to parse recipe: {}", yamlPath);
        return std::nullopt;
    }

    auto parsed = json::parse(parseResp->body, nullptr, false);
    if (parsed.is_discarded()) return std::nullopt;

    // Save recipe to server
    json saveBody;
    saveBody["recipe"] = parsed["recipe"];
    auto saveResp = httpPost("/recipes/save", saveBody);
    if (!saveResp || !saveResp->ok()) {
        LOG_ERROR("AI", "GooseBridge: failed to save recipe: {}", yamlPath);
        return std::nullopt;
    }

    auto saveResult = json::parse(saveResp->body, nullptr, false);
    if (saveResult.contains("id")) {
        return saveResult["id"].get<std::string>();
    }
    return std::nullopt;
}

std::vector<json> GooseBridge::listRecipes() {
    auto resp = httpGet("/recipes/list");
    if (!resp || !resp->ok()) return {};

    auto data = json::parse(resp->body, nullptr, false);
    if (data.is_discarded() || !data.contains("manifests")) return {};

    return data["manifests"].get<std::vector<json>>();
}

// ============================================================================
// Configuration
// ============================================================================

bool GooseBridge::setProvider(const std::string& provider, const std::string& model) {
    json body;
    body["provider"] = provider;
    body["model"] = model;

    auto resp = httpPost("/config/set_provider", body);
    if (resp && resp->ok()) {
        m_config.defaultProvider = provider;
        m_config.defaultModel = model;
        LOG_INFO("AI", "GooseBridge: provider set to {}/{}", provider, model);
        return true;
    }
    return false;
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
// Internal: HTTP Client
// ============================================================================

std::optional<GooseBridge::HttpResponse> GooseBridge::httpGet(const std::string& path) {
    try {
        httplib::Client cli(m_config.host, m_config.port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(30);

        // Disable SSL verification for self-signed certs
        
        httplib::Headers headers = {
            {"X-Secret-Key", m_config.secretKey},
            {"Accept", "application/json"}
        };

        auto result = cli.Get(path, headers);
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
        httplib::Client cli(m_config.host, m_config.port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(60);
        
        httplib::Headers headers = {
            {"X-Secret-Key", m_config.secretKey},
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };

        std::string bodyStr = body.dump();
        auto result = cli.Post(path, headers, bodyStr, "application/json");
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

std::optional<GooseBridge::HttpResponse> GooseBridge::httpPut(
    const std::string& path, const json& body)
{
    try {
        httplib::Client cli(m_config.host, m_config.port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(30);
        
        httplib::Headers headers = {
            {"X-Secret-Key", m_config.secretKey},
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };

        std::string bodyStr = body.dump();
        auto result = cli.Put(path, headers, bodyStr, "application/json");
        if (!result) return std::nullopt;

        return HttpResponse{result->status, result->body};
    } catch (const std::exception& e) {
        LOG_ERROR("AI", "GooseBridge HTTP PUT {} exception: {}", path, e.what());
        return std::nullopt;
    }
}

std::optional<GooseBridge::HttpResponse> GooseBridge::httpDelete(const std::string& path) {
    try {
        httplib::Client cli(m_config.host, m_config.port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(30);
        
        httplib::Headers headers = {
            {"X-Secret-Key", m_config.secretKey}
        };

        auto result = cli.Delete(path, headers);
        if (!result) return std::nullopt;

        return HttpResponse{result->status, result->body};
    } catch (const std::exception& e) {
        LOG_ERROR("AI", "GooseBridge HTTP DELETE {} exception: {}", path, e.what());
        return std::nullopt;
    }
}

// ============================================================================
// Internal: SSE Stream Processing
// ============================================================================

void GooseBridge::processSSEStream(
    const std::string& sessionId,
    const json& requestBody,
    SSECallback callback)
{
    httplib::Client cli(m_config.host, m_config.port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(120);  // Replies can take a while
    
    httplib::Headers headers = {
        {"X-Secret-Key", m_config.secretKey},
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"}
    };

    std::string bodyStr = requestBody.dump();
    json lastMessage;

    // httplib doesn't have Post overloads with ContentReceiver,
    // so we build a Request manually to stream SSE response chunks.
    httplib::Request req;
    req.method = "POST";
    req.path = "/reply";
    req.headers = headers;
    req.body = bodyStr;
    req.set_header("Content-Type", "application/json");

    req.content_receiver = [&](const char* data, size_t len,
                               uint64_t /*offset*/, uint64_t /*total*/) -> bool {
        std::string chunk(data, len);

        // SSE events are separated by double newlines
        // Each event starts with "data: "
        std::istringstream stream(chunk);
        std::string line;

        while (std::getline(stream, line)) {
            // Remove trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.empty()) continue;

            if (line.substr(0, 6) == "data: ") {
                std::string eventData = line.substr(6);
                SSEEvent event = parseSSEEvent(eventData);

                // Track token usage
                if (event.tokenState.has_value()) {
                    std::lock_guard<std::mutex> lock(m_sessionsMutex);
                    auto it = m_sessions.find(sessionId);
                    if (it != m_sessions.end()) {
                        it->second.totalInputTokens =
                            event.tokenState->inputTokens;
                        it->second.totalOutputTokens =
                            event.tokenState->outputTokens;
                    }
                }

                // Extract commands from message events
                if (event.type == SSEEventType::Message) {
                    extractCommands(event.data);
                    lastMessage = event.data;
                }

                // Notify per-message callback
                if (callback) callback(event);

                // On Finish, notify the global callback
                if (event.type == SSEEventType::Finish) {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    if (m_replyFinishCallback) {
                        m_replyFinishCallback(sessionId, lastMessage);
                    }
                }

                // Stop on error
                if (event.type == SSEEventType::Error) {
                    LOG_ERROR("AI", "GooseBridge: SSE error for session {}: {}",
                              sessionId, event.data.dump());
                    return false;
                }
            }
        }
        return true;  // Continue receiving
    };

    auto result = cli.send(req);

    if (!result) {
        LOG_ERROR("AI", "GooseBridge: SSE stream for session {} failed", sessionId);
    }
}

SSEEvent GooseBridge::parseSSEEvent(const std::string& data) {
    SSEEvent event;
    event.data = json::parse(data, nullptr, false);

    if (event.data.is_discarded()) {
        event.type = SSEEventType::Unknown;
        return event;
    }

    // Parse event type from tagged union
    std::string typeStr = event.data.value("type", "");
    if (typeStr == "Message")              event.type = SSEEventType::Message;
    else if (typeStr == "Error")           event.type = SSEEventType::Error;
    else if (typeStr == "Finish")          event.type = SSEEventType::Finish;
    else if (typeStr == "ModelChange")     event.type = SSEEventType::ModelChange;
    else if (typeStr == "Notification")    event.type = SSEEventType::Notification;
    else if (typeStr == "UpdateConversation") event.type = SSEEventType::UpdateConversation;
    else if (typeStr == "Ping")            event.type = SSEEventType::Ping;
    else                                   event.type = SSEEventType::Unknown;

    // Parse token state if present
    if (event.data.contains("token_state")) {
        auto& ts = event.data["token_state"];
        SSEEvent::TokenState tokenState;
        tokenState.inputTokens = ts.value("accumulated_input_tokens", (int64_t)0);
        tokenState.outputTokens = ts.value("accumulated_output_tokens", (int64_t)0);
        tokenState.totalTokens = ts.value("accumulated_total_tokens", (int64_t)0);
        event.tokenState = tokenState;
    }

    return event;
}

void GooseBridge::extractCommands(const json& message) {
    // The AI agent's response contains tool_use results from the MCP extension.
    // These tool calls correspond to engine commands (move_to, say_dialog, etc.)
    // The MCP extension may also push commands directly via the callback HTTP endpoint,
    // but we also parse them from the message for redundancy.

    if (!message.contains("message")) return;
    const auto& msg = message["message"];
    if (!msg.contains("content")) return;

    for (const auto& part : msg["content"]) {
        if (!part.contains("type")) continue;
        std::string partType = part["type"].get<std::string>();

        if (partType == "tool_use") {
            std::string toolName = part.value("name", "");
            json input = part.value("input", json::object());

            // Map tool calls to engine commands
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
            return true;
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

        if (!healthy && m_serverRunning) {
            LOG_WARN("AI", "GooseBridge: goose-server health check failed, attempting restart...");
            killProcess();

            if (launchProcess() && waitForServerReady(m_config.startupTimeoutMs)) {
                LOG_INFO("AI", "GooseBridge: goose-server restarted successfully");
            } else {
                LOG_ERROR("AI", "GooseBridge: failed to restart goose-server");
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
#ifdef _WIN32
    // Set environment variables for the goosed process
    SetEnvironmentVariableA("GOOSE_HOST", m_config.host.c_str());
    SetEnvironmentVariableA("GOOSE_PORT", std::to_string(m_config.port).c_str());
    SetEnvironmentVariableA("GOOSE_SERVER__SECRET_KEY", m_config.secretKey.c_str());

    std::string cmdLine = m_config.goosedPath + " agent";

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    // Redirect stdout/stderr for debugging
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hidden window

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

    LOG_INFO("AI", "GooseBridge: launched goosed process (PID: {})", pi.dwProcessId);
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("AI", "GooseBridge: fork() failed");
        return false;
    }

    if (pid == 0) {
        // Child process
        setenv("GOOSE_HOST", m_config.host.c_str(), 1);
        setenv("GOOSE_PORT", std::to_string(m_config.port).c_str(), 1);
        setenv("GOOSE_SERVER__SECRET_KEY", m_config.secretKey.c_str(), 1);

        execlp(m_config.goosedPath.c_str(), "goosed", "agent", nullptr);
        _exit(1);  // exec failed
    }

    m_processPid = pid;
    LOG_INFO("AI", "GooseBridge: launched goosed process (PID: {})", pid);
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
        // Wait for graceful shutdown
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
} // namespace VulkanCube
