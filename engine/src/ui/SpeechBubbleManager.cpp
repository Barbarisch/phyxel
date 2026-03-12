#include "ui/SpeechBubbleManager.h"
#include "core/EntityRegistry.h"
#include "scene/Entity.h"

namespace Phyxel {
namespace UI {

void SpeechBubbleManager::say(const std::string& entityId, const std::string& text, float duration) {
    // Evict oldest if at capacity
    while (m_bubbles.size() >= MAX_BUBBLES) {
        m_bubbles.erase(m_bubbles.begin());
    }

    SpeechBubble bubble;
    bubble.text = text;
    bubble.speakerEntityId = entityId;
    bubble.lifetime = duration;
    bubble.elapsed = 0.0f;
    bubble.fadeStartTime = duration * 0.7f; // fade starts at 70% of lifetime
    m_bubbles.push_back(std::move(bubble));
}

void SpeechBubbleManager::update(float dt) {
    for (auto& bubble : m_bubbles) {
        bubble.elapsed += dt;
    }

    // Remove expired bubbles
    m_bubbles.erase(
        std::remove_if(m_bubbles.begin(), m_bubbles.end(),
            [](const SpeechBubble& b) { return b.elapsed >= b.lifetime; }),
        m_bubbles.end());
}

glm::vec3 SpeechBubbleManager::getBubbleWorldPosition(const SpeechBubble& bubble) const {
    if (!m_registry) return glm::vec3(0);
    auto* entity = m_registry->getEntity(bubble.speakerEntityId);
    if (!entity) return glm::vec3(0);
    // Position above entity head
    return entity->getPosition() + glm::vec3(0.0f, 2.5f, 0.0f);
}

float SpeechBubbleManager::getBubbleOpacity(const SpeechBubble& bubble) const {
    if (bubble.elapsed < bubble.fadeStartTime) return 1.0f;
    float fadeProgress = (bubble.elapsed - bubble.fadeStartTime) /
                         (bubble.lifetime - bubble.fadeStartTime);
    return 1.0f - glm::clamp(fadeProgress, 0.0f, 1.0f);
}

} // namespace UI
} // namespace Phyxel
