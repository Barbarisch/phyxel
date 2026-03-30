#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

namespace Core { class EntityRegistry; }
namespace Scene { class Entity; }

namespace AI {

/// Callback type for voxel line-of-sight checks.
/// Returns true if the ray from `from` to `to` is unobstructed.
using LOSCheckFn = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;

/// Callback type for drawing debug FOV cone lines each frame.
/// Parameters: npcPos, npcForward, visionRange, halfAngleDeg, hasThreat
using DebugConeFn = std::function<void(const glm::vec3& pos, const glm::vec3& forward,
                                        float range, float halfAngleDeg, bool hasThreat)>;

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

    /// Optional line-of-sight checker (voxel raycast). If set, entities inside
    /// the vision cone are only marked visible when LOS is clear.
    LOSCheckFn losCheck;

    /// Optional debug cone drawer. Called every perception tick to visualize FOV.
    DebugConeFn debugConeDraw;

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
    glm::vec3 m_lastForward{0, 0, -1};  ///< Cached for debug drawing between ticks

    void perceive(const glm::vec3& npcPos,
                  const glm::vec3& npcForward,
                  const std::string& selfId,
                  Core::EntityRegistry& registry);

    void decayMemory(float dt);
};

} // namespace AI
} // namespace Phyxel
