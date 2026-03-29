#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

namespace Core { class EntityRegistry; }
namespace Scene { class Entity; }

namespace AI {

// ============================================================================
// SenseResult — what the NPC knows about a detected entity
// ============================================================================
struct SenseResult {
    std::string entityId;
    glm::vec3   position{0};
    float       distance  = 0.0f;
    float       angle     = 0.0f;   // degrees from NPC forward
    bool        isVisible = false;
    float       lastSeen  = 0.0f;   // seconds since last perception
    float       threatLevel = 0.0f; // 0 = none, 1 = max

    nlohmann::json toJson() const;
};

// ============================================================================
// PerceptionComponent — per-NPC sensory state
// ============================================================================
class PerceptionComponent {
public:
    // ── Configuration ───────────────────────────────────────────────────
    float visionRange = 15.0f;       // units
    float visionAngle = 120.0f;      // degrees (total cone, not half)
    float hearingRange = 20.0f;      // units (omnidirectional)
    float memoryDuration = 10.0f;    // seconds before forgotten
    float updateInterval = 0.25f;    // seconds between perception ticks

    // ── Update (called each frame, batches internally) ──────────────────
    void update(float dt,
                const glm::vec3& npcPos,
                const glm::vec3& npcForward,
                const std::string& selfId,
                Core::EntityRegistry& registry);

    // ── Queries ─────────────────────────────────────────────────────────
    const std::unordered_map<std::string, SenseResult>& getKnownEntities() const { return m_known; }

    std::vector<SenseResult> getVisibleEntities() const;
    std::vector<SenseResult> getHeardEntities() const;
    std::vector<SenseResult> getThreats(float minThreat = 0.1f) const;

    bool isAwareOf(const std::string& entityId) const;
    const SenseResult* getSense(const std::string& entityId) const;

    /// Manually tag threat level (e.g., from combat events).
    void setThreatLevel(const std::string& entityId, float level);

    nlohmann::json toJson() const;

private:
    std::unordered_map<std::string, SenseResult> m_known;
    float m_timer = 0.0f;

    void perceive(const glm::vec3& npcPos,
                  const glm::vec3& npcForward,
                  const std::string& selfId,
                  Core::EntityRegistry& registry);

    void decayMemory(float dt);
};

} // namespace AI
} // namespace Phyxel
