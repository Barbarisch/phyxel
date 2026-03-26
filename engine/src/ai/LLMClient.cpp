#include "ai/LLMClient.h"
#include "utils/Logger.h"

#include <sstream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    // On Linux/Mac, fall back to cpp-httplib with OpenSSL
    #define CPPHTTPLIB_OPENSSL_SUPPORT
    #include <httplib.h>
#endif

namespace Phyxel {
namespace AI {

// ============================================================================
// Default Models
// ============================================================================

std::string LLMConfig::getDefaultModel(const std::string& provider) {
    if (provider == "anthropic") return "claude-sonnet-4-20250514";
    if (provider == "openai")    return "gpt-4o";
    if (provider == "ollama")    return "llama3.2";
    return "";
}

// ============================================================================
// Construction
// ============================================================================

LLMClient::LLMClient(const LLMConfig& config)
    : m_config(config)
{
    LOG_INFO("AI", "LLMClient created (provider={}, model={})",
             m_config.provider,
             m_config.model.empty() ? LLMConfig::getDefaultModel(m_config.provider) : m_config.model);
}

LLMClient::~LLMClient() = default;

// ============================================================================
// Public API
// ============================================================================

LLMResponse LLMClient::complete(const std::vector<LLMMessage>& messages) {
    if (!isConfigured()) {
        return LLMResponse{"", "", 0, 0, "LLMClient not configured (missing API key or provider)"};
    }

    LLMResponse resp;
    if (m_config.provider == "anthropic") {
        resp = callAnthropic(messages);
    } else if (m_config.provider == "openai") {
        resp = callOpenAI(messages);
    } else if (m_config.provider == "ollama") {
        resp = callOllama(messages);
    } else {
        resp.error = "Unknown provider: " + m_config.provider;
    }

    // Track usage
    if (resp.ok()) {
        std::lock_guard<std::mutex> lock(m_usageMutex);
        m_usage.totalInput += resp.inputTokens;
        m_usage.totalOutput += resp.outputTokens;
        m_usage.totalCalls++;
    }

    return resp;
}

std::future<LLMResponse> LLMClient::completeAsync(const std::vector<LLMMessage>& messages) {
    return std::async(std::launch::async, [this, msgs = messages]() {
        return complete(msgs);
    });
}

void LLMClient::setConfig(const LLMConfig& config) {
    m_config = config;
    LOG_INFO("AI", "LLMClient config updated (provider={}, model={})",
             m_config.provider,
             m_config.model.empty() ? LLMConfig::getDefaultModel(m_config.provider) : m_config.model);
}

bool LLMClient::isConfigured() const {
    if (m_config.provider == "ollama") return true;
    return !m_config.apiKey.empty();
}

LLMClient::TokenUsage LLMClient::getTokenUsage() const {
    std::lock_guard<std::mutex> lock(m_usageMutex);
    return m_usage;
}

// ============================================================================
// Anthropic Claude API
// ============================================================================

LLMResponse LLMClient::callAnthropic(const std::vector<LLMMessage>& messages) {
    std::string model = m_config.model.empty()
        ? LLMConfig::getDefaultModel("anthropic") : m_config.model;

    // Build request body
    // Anthropic requires system message separate from messages array
    json body;
    body["model"] = model;
    body["max_tokens"] = m_config.maxTokens;
    body["temperature"] = m_config.temperature;

    json msgArray = json::array();
    for (const auto& msg : messages) {
        if (msg.role == "system") {
            body["system"] = msg.content;
        } else {
            json m;
            m["role"] = msg.role;
            m["content"] = msg.content;
            msgArray.push_back(m);
        }
    }
    body["messages"] = msgArray;

    std::vector<std::pair<std::string, std::string>> headers = {
        {"x-api-key", m_config.apiKey},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };

    auto result = httpsPost("api.anthropic.com", 443, "/v1/messages",
                            body.dump(), headers);

    LLMResponse resp;
    if (!result.ok()) {
        resp.error = result.error.empty()
            ? ("HTTP " + std::to_string(result.statusCode) + ": " + result.body)
            : result.error;
        return resp;
    }

    // Parse response
    auto j = json::parse(result.body, nullptr, false);
    if (j.is_discarded()) {
        resp.error = "Failed to parse Anthropic response";
        return resp;
    }

    // Check for API error
    if (j.contains("error")) {
        resp.error = j["error"].value("message", "Unknown Anthropic error");
        return resp;
    }

    // Extract content
    if (j.contains("content") && j["content"].is_array() && !j["content"].empty()) {
        for (const auto& block : j["content"]) {
            if (block.value("type", "") == "text") {
                resp.content += block.value("text", "");
            }
        }
    }

    resp.stopReason = j.value("stop_reason", "");
    if (j.contains("usage")) {
        resp.inputTokens = j["usage"].value("input_tokens", 0);
        resp.outputTokens = j["usage"].value("output_tokens", 0);
    }

    return resp;
}

// ============================================================================
// OpenAI API
// ============================================================================

LLMResponse LLMClient::callOpenAI(const std::vector<LLMMessage>& messages) {
    std::string model = m_config.model.empty()
        ? LLMConfig::getDefaultModel("openai") : m_config.model;

    json body;
    body["model"] = model;
    body["max_tokens"] = m_config.maxTokens;
    body["temperature"] = m_config.temperature;

    json msgArray = json::array();
    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgArray.push_back(m);
    }
    body["messages"] = msgArray;

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + m_config.apiKey},
        {"content-type", "application/json"},
    };

    auto result = httpsPost("api.openai.com", 443, "/v1/chat/completions",
                            body.dump(), headers);

    LLMResponse resp;
    if (!result.ok()) {
        resp.error = result.error.empty()
            ? ("HTTP " + std::to_string(result.statusCode) + ": " + result.body)
            : result.error;
        return resp;
    }

    auto j = json::parse(result.body, nullptr, false);
    if (j.is_discarded()) {
        resp.error = "Failed to parse OpenAI response";
        return resp;
    }

    if (j.contains("error")) {
        resp.error = j["error"].value("message", "Unknown OpenAI error");
        return resp;
    }

    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        auto& choice = j["choices"][0];
        resp.content = choice["message"].value("content", "");
        resp.stopReason = choice.value("finish_reason", "");
    }

    if (j.contains("usage")) {
        resp.inputTokens = j["usage"].value("prompt_tokens", 0);
        resp.outputTokens = j["usage"].value("completion_tokens", 0);
    }

    return resp;
}

// ============================================================================
// Ollama API (local, no TLS)
// ============================================================================

LLMResponse LLMClient::callOllama(const std::vector<LLMMessage>& messages) {
    std::string model = m_config.model.empty()
        ? LLMConfig::getDefaultModel("ollama") : m_config.model;

    json body;
    body["model"] = model;
    body["stream"] = false;

    if (m_config.maxTokens > 0) {
        body["options"]["num_predict"] = m_config.maxTokens;
    }
    if (m_config.temperature >= 0.0f) {
        body["options"]["temperature"] = m_config.temperature;
    }

    json msgArray = json::array();
    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgArray.push_back(m);
    }
    body["messages"] = msgArray;

    // Parse host URL to get host and port
    std::string host = "localhost";
    int port = 11434;
    std::string ollamaUrl = m_config.ollamaHost;
    // Strip http://
    if (ollamaUrl.substr(0, 7) == "http://") {
        ollamaUrl = ollamaUrl.substr(7);
    }
    auto colonPos = ollamaUrl.find(':');
    if (colonPos != std::string::npos) {
        host = ollamaUrl.substr(0, colonPos);
        port = std::stoi(ollamaUrl.substr(colonPos + 1));
    } else {
        host = ollamaUrl;
    }

    std::vector<std::pair<std::string, std::string>> headers = {
        {"content-type", "application/json"},
    };

    auto result = httpPost(host, port, "/api/chat", body.dump(), headers);

    LLMResponse resp;
    if (!result.ok()) {
        resp.error = result.error.empty()
            ? ("HTTP " + std::to_string(result.statusCode) + ": " + result.body)
            : result.error;
        return resp;
    }

    auto j = json::parse(result.body, nullptr, false);
    if (j.is_discarded()) {
        resp.error = "Failed to parse Ollama response";
        return resp;
    }

    if (j.contains("message")) {
        resp.content = j["message"].value("content", "");
    }

    resp.stopReason = j.value("done_reason", "stop");
    resp.outputTokens = j.value("eval_count", 0);
    resp.inputTokens = j.value("prompt_eval_count", 0);

    return resp;
}

// ============================================================================
// HTTPS Transport — WinHTTP (Windows)
// ============================================================================

#ifdef _WIN32

LLMClient::HttpResult LLMClient::httpsPost(
    const std::string& host, int port,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers,
    bool useTLS)
{
    HttpResult result;

    // Convert host to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wHost(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wHost[0], wideLen);
    wHost.resize(wideLen - 1); // Remove null

    wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wPath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], wideLen);
    wPath.resize(wideLen - 1);

    // Open WinHTTP session
    HINTERNET hSession = WinHttpOpen(
        L"Phyxel/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        result.error = "WinHttpOpen failed: " + std::to_string(GetLastError());
        return result;
    }

    // Set timeouts
    WinHttpSetTimeouts(hSession,
                       m_config.timeoutMs,  // resolve
                       m_config.timeoutMs,  // connect
                       m_config.timeoutMs,  // send
                       m_config.timeoutMs); // receive

    // Connect
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
                                        static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        result.error = "WinHttpConnect failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Create request
    DWORD flags = useTLS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!hRequest) {
        result.error = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Add headers
    for (const auto& [key, value] : headers) {
        std::string headerLine = key + ": " + value;
        wideLen = MultiByteToWideChar(CP_UTF8, 0, headerLine.c_str(), -1, nullptr, 0);
        std::wstring wHeader(wideLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, headerLine.c_str(), -1, &wHeader[0], wideLen);
        wHeader.resize(wideLen - 1);
        WinHttpAddRequestHeaders(hRequest, wHeader.c_str(), (ULONG)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send request
    BOOL sent = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)body.data(), (DWORD)body.size(),
        (DWORD)body.size(),
        0);
    if (!sent) {
        result.error = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Receive response
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        result.error = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = static_cast<int>(statusCode);

    // Read body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            responseBody.append(buffer.data(), bytesRead);
        }
    }
    result.body = std::move(responseBody);

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

LLMClient::HttpResult LLMClient::httpPost(
    const std::string& host, int port,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers)
{
    // For local HTTP (Ollama), use WinHTTP without TLS
    return httpsPost(host, port, path, body, headers, /*useTLS=*/false);
}

#else
// ============================================================================
// HTTPS Transport — cpp-httplib with OpenSSL (Linux/Mac)
// ============================================================================

LLMClient::HttpResult LLMClient::httpsPost(
    const std::string& host, int port,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers,
    bool useTLS)
{
    HttpResult result;
    try {
        std::unique_ptr<httplib::Client> cli;
        if (useTLS) {
            cli = std::make_unique<httplib::SSLClient>(host, port);
        } else {
            cli = std::make_unique<httplib::Client>(host, port);
        }
        cli->set_connection_timeout(m_config.timeoutMs / 1000);
        cli->set_read_timeout(m_config.timeoutMs / 1000);

        httplib::Headers hdrs;
        for (const auto& [key, value] : headers) {
            hdrs.emplace(key, value);
        }

        auto resp = cli->Post(path, hdrs, body, "application/json");
        if (!resp) {
            result.error = "Connection failed";
            return result;
        }
        result.statusCode = resp->status;
        result.body = resp->body;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

LLMClient::HttpResult LLMClient::httpPost(
    const std::string& host, int port,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers)
{
    return httpsPost(host, port, path, body, headers, false);
}

#endif

} // namespace AI
} // namespace Phyxel
