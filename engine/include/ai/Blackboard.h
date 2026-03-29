#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <optional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

// ============================================================================
// Blackboard — Per-NPC key/value store shared between behavior tree nodes
// ============================================================================

using BlackboardValue = std::variant<bool, int, float, std::string, glm::vec3>;

class Blackboard {
public:
    // ── Setters (typed) ──────────────────────────────────────────────────
    void setBool(const std::string& key, bool v)              { m_data[key] = v; }
    void setInt(const std::string& key, int v)                { m_data[key] = v; }
    void setFloat(const std::string& key, float v)            { m_data[key] = v; }
    void setString(const std::string& key, const std::string& v) { m_data[key] = v; }
    void setVec3(const std::string& key, const glm::vec3& v)  { m_data[key] = v; }

    // ── Overloaded set() for convenience ────────────────────────────────
    void set(const std::string& key, bool v)              { m_data[key] = v; }
    void set(const std::string& key, int v)               { m_data[key] = v; }
    void set(const std::string& key, float v)             { m_data[key] = v; }
    void set(const std::string& key, const std::string& v) { m_data[key] = v; }
    void set(const std::string& key, const glm::vec3& v)  { m_data[key] = v; }

    // ── Getters (return default if missing or wrong type) ───────────────
    bool getBool(const std::string& key, bool def = false) const {
        auto it = m_data.find(key);
        if (it != m_data.end() && std::holds_alternative<bool>(it->second))
            return std::get<bool>(it->second);
        return def;
    }

    int getInt(const std::string& key, int def = 0) const {
        auto it = m_data.find(key);
        if (it != m_data.end() && std::holds_alternative<int>(it->second))
            return std::get<int>(it->second);
        return def;
    }

    float getFloat(const std::string& key, float def = 0.0f) const {
        auto it = m_data.find(key);
        if (it != m_data.end() && std::holds_alternative<float>(it->second))
            return std::get<float>(it->second);
        return def;
    }

    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = m_data.find(key);
        if (it != m_data.end() && std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return def;
    }

    glm::vec3 getVec3(const std::string& key, const glm::vec3& def = glm::vec3(0)) const {
        auto it = m_data.find(key);
        if (it != m_data.end() && std::holds_alternative<glm::vec3>(it->second))
            return std::get<glm::vec3>(it->second);
        return def;
    }

    // ── Queries ─────────────────────────────────────────────────────────
    bool has(const std::string& key) const { return m_data.find(key) != m_data.end(); }
    void erase(const std::string& key)     { m_data.erase(key); }
    void clear()                           { m_data.clear(); }
    size_t size() const                    { return m_data.size(); }

    // ── Serialization ───────────────────────────────────────────────────
    nlohmann::json toJson() const {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& [key, val] : m_data) {
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, glm::vec3>) {
                    j[key] = {{"x", v.x}, {"y", v.y}, {"z", v.z}};
                } else {
                    j[key] = v;
                }
            }, val);
        }
        return j;
    }

private:
    std::unordered_map<std::string, BlackboardValue> m_data;
};

} // namespace AI
} // namespace Phyxel
