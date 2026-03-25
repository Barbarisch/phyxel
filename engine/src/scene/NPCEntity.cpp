#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "graphics/LightManager.h"
#include "core/EntityRegistry.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Scene {

NPCEntity::NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
                     const std::string& name, const std::string& animFile)
    : m_name(name)
{
    m_character = std::make_unique<AnimatedVoxelCharacter>(physicsWorld, position);
    if (!m_character->loadModel(animFile)) {
        LOG_WARN("NPCEntity", "Failed to load anim file '{}' for NPC '{}'", animFile, name);
    } else {
        m_character->playAnimation("idle");
    }
    this->position = position;
}

NPCEntity::~NPCEntity() = default;

void NPCEntity::update(float deltaTime) {
    // Delegate to behavior
    if (m_behavior) {
        m_context.self = this;
        m_behavior->update(deltaTime, m_context);
    }

    // Update inner animated character
    if (m_character) {
        m_character->update(deltaTime);
        // Sync position from character (it may have moved via physics)
        this->position = m_character->getPosition();
    }

    // Update attached light position
    if (m_attachedLightId >= 0 && m_context.lightManager) {
        m_context.lightManager->updatePointLightPosition(m_attachedLightId, getPosition() + glm::vec3(0, 2, 0));
    }
}

void NPCEntity::render(Graphics::RenderCoordinator* renderer) {
    if (m_character) {
        m_character->render(renderer);
    }
}

void NPCEntity::setPosition(const glm::vec3& pos) {
    Entity::setPosition(pos);
    if (m_character) {
        m_character->setPosition(pos);
    }
}

glm::vec3 NPCEntity::getPosition() const {
    if (m_character) {
        return m_character->getPosition();
    }
    return position;
}

void NPCEntity::setMoveVelocity(const glm::vec3& velocity) {
    if (m_character) {
        m_character->setMoveVelocity(velocity);
    }
}

void NPCEntity::setBehavior(std::unique_ptr<NPCBehavior> behavior) {
    m_behavior = std::move(behavior);
}

void NPCEntity::setContext(Core::EntityRegistry* registry, Graphics::LightManager* lightManager,
                           UI::SpeechBubbleManager* speechBubbleManager, const std::string& entityId) {
    m_context.entityRegistry = registry;
    m_context.lightManager = lightManager;
    m_context.speechBubbleManager = speechBubbleManager;
    m_context.selfId = entityId;
    m_context.self = this;

    if (registry) {
        m_context.getEntityPosition = [registry](const std::string& id) -> glm::vec3 {
            auto* entity = registry->getEntity(id);
            return entity ? entity->getPosition() : glm::vec3(0.0f);
        };
    }
}

} // namespace Scene
} // namespace Phyxel
