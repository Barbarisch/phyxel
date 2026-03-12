#include "core/NPCManager.h"
#include "scene/NPCEntity.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "core/EntityRegistry.h"
#include "graphics/LightManager.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

Scene::NPCEntity* NPCManager::spawnNPC(const std::string& name, const std::string& animFile,
                                        const glm::vec3& position, NPCBehaviorType behaviorType,
                                        const std::vector<glm::vec3>& waypoints,
                                        float walkSpeed, float waitTime) {
    std::unique_ptr<Scene::NPCBehavior> behavior;

    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }

    return spawnNPCWithBehavior(name, animFile, position, std::move(behavior));
}

Scene::NPCEntity* NPCManager::spawnNPCWithBehavior(const std::string& name, const std::string& animFile,
                                                     const glm::vec3& position,
                                                     std::unique_ptr<Scene::NPCBehavior> behavior) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }

    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, position, name, animFile);
    npc->setBehavior(std::move(behavior));

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }

    // Wire context
    npc->setContext(m_entityRegistry, m_lightManager, entityId);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned NPC '{}' at ({}, {}, {})", name, position.x, position.y, position.z);
    return rawPtr;
}

bool NPCManager::removeNPC(const std::string& name) {
    auto it = m_npcs.find(name);
    if (it == m_npcs.end()) return false;

    // Unregister from EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->unregisterEntity(entityId);
    }

    // Remove attached light
    if (it->second->getAttachedLightId() >= 0 && m_lightManager) {
        m_lightManager->removeLight(it->second->getAttachedLightId());
    }

    m_npcs.erase(it);
    LOG_INFO("NPCManager", "Removed NPC '{}'", name);
    return true;
}

Scene::NPCEntity* NPCManager::getNPC(const std::string& name) const {
    auto it = m_npcs.find(name);
    return (it != m_npcs.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> NPCManager::getAllNPCNames() const {
    std::vector<std::string> names;
    names.reserve(m_npcs.size());
    for (const auto& [name, _] : m_npcs) {
        names.push_back(name);
    }
    return names;
}

void NPCManager::update(float deltaTime) {
    for (auto& [name, npc] : m_npcs) {
        npc->update(deltaTime);
    }
}


} // namespace Core
} // namespace Phyxel
