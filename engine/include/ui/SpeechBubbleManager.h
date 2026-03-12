#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Core { class EntityRegistry; }

namespace UI {

/// A single floating speech bubble above an entity's head.
struct SpeechBubble {
    std::string text;
    std::string speakerEntityId;
    float lifetime;       ///< Total duration in seconds
    float elapsed = 0.0f; ///< Time elapsed since creation
    float fadeStartTime;  ///< When fade-out begins (relative to lifetime)
};

/// Manages floating speech bubbles rendered above entities in world space.
class SpeechBubbleManager {
public:
    SpeechBubbleManager() = default;

    /// Set the entity registry for position lookup.
    void setEntityRegistry(Core::EntityRegistry* registry) { m_registry = registry; }

    /// Create a new speech bubble above an entity.
    /// @param entityId  The speaking entity's registry ID
    /// @param text      Text to display
    /// @param duration  How long the bubble lasts (seconds)
    void say(const std::string& entityId, const std::string& text, float duration = 3.0f);

    /// Update all bubbles — age them and remove expired ones.
    void update(float dt);

    /// Get all currently active bubbles (for rendering).
    const std::vector<SpeechBubble>& getBubbles() const { return m_bubbles; }

    /// Get current bubble count.
    size_t getBubbleCount() const { return m_bubbles.size(); }

    /// Get world position of a bubble's speaker entity.
    /// Returns (0,0,0) if entity not found.
    glm::vec3 getBubbleWorldPosition(const SpeechBubble& bubble) const;

    /// Calculate opacity for a bubble (1.0 full, fades to 0.0).
    float getBubbleOpacity(const SpeechBubble& bubble) const;

    /// Maximum simultaneous bubbles (oldest evicted when exceeded).
    static constexpr size_t MAX_BUBBLES = 8;

private:
    Core::EntityRegistry* m_registry = nullptr;
    std::vector<SpeechBubble> m_bubbles;
};

} // namespace UI
} // namespace Phyxel
