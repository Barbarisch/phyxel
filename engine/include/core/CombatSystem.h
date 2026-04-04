#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

namespace Scene { class Entity; }
namespace Core { class EntityRegistry; }

namespace Core {

// ============================================================================
// Damage types
// ============================================================================
enum class DamageType {
    Physical,
    Fire,
    Ice,
    Poison
};

inline const char* damageTypeToString(DamageType type) {
    switch (type) {
        case DamageType::Physical: return "Physical";
        case DamageType::Fire:     return "Fire";
        case DamageType::Ice:      return "Ice";
        case DamageType::Poison:   return "Poison";
        default:                   return "Unknown";
    }
}

// ============================================================================
// DamageEvent — dispatched when damage is dealt
// ============================================================================
struct DamageEvent {
    std::string attackerId;
    std::string targetId;
    float amount = 0.0f;
    float actualDamage = 0.0f; // After armor/resistance
    DamageType type = DamageType::Physical;
    glm::vec3 knockback{0.0f};
    bool killed = false;
    std::string hitBone;  // Name of the bone AABB that was hit (empty if not bone-tested)

    nlohmann::json toJson() const {
        nlohmann::json j = {
            {"attackerId", attackerId},
            {"targetId", targetId},
            {"amount", amount},
            {"actualDamage", actualDamage},
            {"type", damageTypeToString(type)},
            {"killed", killed}
        };
        if (!hitBone.empty()) j["hitBone"] = hitBone;
        return j;
    }
};

// ============================================================================
// CombatSystem — handles attack resolution and damage delivery
// ============================================================================
class CombatSystem {
public:
    /// Attack parameters for a single strike.
    struct AttackParams {
        std::string attackerId;
        glm::vec3 attackerPos{0.0f};
        glm::vec3 attackerForward{0.0f, 0.0f, 1.0f};
        float damage = 1.0f;
        float reach = 1.5f;
        float coneAngleDeg = 90.0f;    // How wide the attack cone is
        DamageType damageType = DamageType::Physical;
        float knockbackForce = 2.0f;
    };

    /// Perform an attack: find targets in range/cone, deal damage.
    /// Returns list of DamageEvents for all entities hit.
    std::vector<DamageEvent> performAttack(
        const AttackParams& params,
        EntityRegistry& registry);

    /// Get/set invulnerability duration (seconds after taking damage).
    float getInvulnerabilityDuration() const { return m_invulnDuration; }
    void setInvulnerabilityDuration(float seconds) { m_invulnDuration = seconds; }

    /// Check if an entity is still in invulnerability frames.
    bool isInvulnerable(const std::string& entityId) const;

    /// Update invulnerability timers. Call each frame.
    void update(float dt);

    /// Callback for damage events (for game events, sound, VFX, etc.)
    using OnDamageCallback = std::function<void(const DamageEvent&)>;
    void setOnDamage(OnDamageCallback cb) { m_onDamage = std::move(cb); }

private:
    float m_invulnDuration = 0.5f;
    std::unordered_map<std::string, float> m_invulnTimers; // entityId → remaining time
    OnDamageCallback m_onDamage;
};

} // namespace Core
} // namespace Phyxel
