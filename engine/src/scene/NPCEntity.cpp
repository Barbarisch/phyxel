#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/VoxelCharacter.h"
#include "scene/RagdollCharacter.h"
#include "scene/CharacterAppearance.h"
#include "graphics/LightManager.h"
#include "core/EntityRegistry.h"
#include "utils/Logger.h"

using Phyxel::Scene::DriveMode;

namespace Phyxel {
namespace Scene {

NPCEntity::NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
                     const std::string& name, const std::string& animFile,
                     const CharacterAppearance& appearance)
    : m_name(name)
{
    m_character = std::make_unique<AnimatedVoxelCharacter>(physicsWorld, position);
    m_character->setAppearance(appearance);
    if (!m_character->loadModel(animFile)) {
        LOG_WARN("NPCEntity", "Failed to load anim file '{}' for NPC '{}'", animFile, name);
    } else {
        m_character->playAnimation("idle");
    }
    this->position = position;
    m_needs.initDefaults();
}

NPCEntity::NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
                     const std::string& name, const CharacterAppearance& appearance,
                     const Phyxel::Skeleton& skeleton, const Phyxel::VoxelModel& model,
                     const std::vector<Phyxel::AnimationClip>& clips)
    : m_name(name)
{
    m_character = std::make_unique<AnimatedVoxelCharacter>(physicsWorld, position);
    m_character->setAppearance(appearance);
    if (!m_character->loadFromSkeleton(skeleton, model, clips)) {
        LOG_WARN("NPCEntity", "Failed to load skeleton template for NPC '{}'", name);
    } else {
        m_character->playAnimation("idle");
    }
    this->position = position;
    m_needs.initDefaults();
}

NPCEntity::NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
                     const std::string& name, const std::string& animFile,
                     const CharacterAppearance& appearance, bool physicsDriven)
    : m_name(name)
{
    m_voxelCharacter = std::make_unique<VoxelCharacter>(physicsWorld, position);
    m_voxelCharacter->setAppearance(appearance);
    if (!m_voxelCharacter->loadModel(animFile)) {
        LOG_WARN("NPCEntity", "Failed to load anim file '{}' for physics NPC '{}'", animFile, name);
    } else {
        m_voxelCharacter->setDriveMode(DriveMode::Physics);
        m_voxelCharacter->playAnimation("idle");
    }
    this->position = position;
    m_needs.initDefaults();
}

NPCEntity::NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
                     const std::string& name, const CharacterAppearance& appearance,
                     const Phyxel::Skeleton& skeleton, const Phyxel::VoxelModel& model,
                     const std::vector<Phyxel::AnimationClip>& clips, bool physicsDriven)
    : m_name(name)
{
    m_voxelCharacter = std::make_unique<VoxelCharacter>(physicsWorld, position);
    m_voxelCharacter->setAppearance(appearance);
    if (!m_voxelCharacter->loadFromData(skeleton, model, clips)) {
        LOG_WARN("NPCEntity", "Failed to load skeleton template for physics NPC '{}'", name);
    } else {
        m_voxelCharacter->setDriveMode(DriveMode::Physics);
        m_voxelCharacter->playAnimation("idle");
    }
    this->position = position;
    m_needs.initDefaults();
}

NPCEntity::~NPCEntity() = default;

void NPCEntity::update(float deltaTime) {
    // Delegate to behavior
    if (m_behavior) {
        m_context.self = this;
        m_behavior->update(deltaTime, m_context);
    }

    // Update inner character (animated or physics-driven)
    if (m_voxelCharacter) {
        m_voxelCharacter->update(deltaTime);
        this->position = m_voxelCharacter->getPosition();
    } else if (m_character) {
        m_character->update(deltaTime);
        this->position = m_character->getPosition();
    }

    // Update attached light position
    if (m_attachedLightId >= 0 && m_context.lightManager) {
        m_context.lightManager->updatePointLightPosition(m_attachedLightId, getPosition() + glm::vec3(0, 2, 0));
    }
}

void NPCEntity::render(Graphics::RenderCoordinator* renderer) {
    if (m_voxelCharacter) {
        m_voxelCharacter->render(renderer);
    } else if (m_character) {
        m_character->render(renderer);
    }
}

void NPCEntity::setPosition(const glm::vec3& pos) {
    Entity::setPosition(pos);
    if (m_voxelCharacter) {
        m_voxelCharacter->setPosition(pos);
    } else if (m_character) {
        m_character->setPosition(pos);
    }
}

glm::vec3 NPCEntity::getPosition() const {
    if (m_voxelCharacter) {
        return m_voxelCharacter->getPosition();
    }
    if (m_character) {
        return m_character->getPosition();
    }
    return position;
}

void NPCEntity::setMoveVelocity(const glm::vec3& velocity) {
    if (m_voxelCharacter) {
        m_voxelCharacter->setMoveVelocity(velocity);
    } else if (m_character) {
        m_character->setMoveVelocity(velocity);
    }
}

RagdollCharacter* NPCEntity::getRenderableCharacter() const {
    if (m_voxelCharacter) return m_voxelCharacter.get();
    if (m_character) return m_character.get();
    return nullptr;
}

void NPCEntity::setBehavior(std::unique_ptr<NPCBehavior> behavior) {
    m_behavior = std::move(behavior);
}

void NPCEntity::setContext(Core::EntityRegistry* registry, Graphics::LightManager* lightManager,
                           UI::SpeechBubbleManager* speechBubbleManager, const std::string& entityId,
                           Graphics::DayNightCycle* dayNightCycle,
                           Core::LocationRegistry* locationRegistry,
                           ChunkManager* chunkManager,
                           RaycastVisualizer* raycastVisualizer) {
    m_context.entityRegistry = registry;
    m_context.lightManager = lightManager;
    m_context.speechBubbleManager = speechBubbleManager;
    m_context.selfId = entityId;
    m_context.self = this;
    m_context.dayNightCycle = dayNightCycle;
    m_context.locationRegistry = locationRegistry;
    m_context.chunkManager = chunkManager;
    m_context.raycastVisualizer = raycastVisualizer;

    if (registry) {
        m_context.getEntityPosition = [registry](const std::string& id) -> glm::vec3 {
            auto* entity = registry->getEntity(id);
            return entity ? entity->getPosition() : glm::vec3(0.0f);
        };
    }
}

} // namespace Scene
} // namespace Phyxel
