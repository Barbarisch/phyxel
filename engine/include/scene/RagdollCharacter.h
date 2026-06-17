#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include "core/HealthComponent.h"
#include "utils/Logger.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {
namespace Scene {

enum class Faction {
    Player,
    Enemy,
    Neutral
};

struct RagdollPart {
    glm::vec3 scale;
    glm::vec4 color;
    std::string name;
    glm::vec3 offset = glm::vec3(0.0f);
    bool active = true;
    // Direct-transform path used by AnimatedVoxelCharacter bones.
    bool      useDirectTransform = true;
    int       boneGroupId = -1;
    glm::vec3 worldPos = glm::vec3(0.0f);
    glm::quat worldRot = glm::quat(1, 0, 0, 0);
};

class RagdollCharacter : public Entity {
public:
    RagdollCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
        : physicsWorld(physicsWorld), faction(Faction::Neutral),
          m_health(std::make_unique<Core::HealthComponent>(100.0f)) {}

    virtual ~RagdollCharacter() = default;

    virtual void update(float deltaTime) override = 0;
    virtual void render(Graphics::RenderCoordinator* renderer) override {}

    virtual void setPosition(const glm::vec3& pos) override {}

    virtual glm::vec3 getPosition() const override { return glm::vec3(0.0f); }

    const std::vector<RagdollPart>& getParts() const { return parts; }

    // Direct-transform parts grouped by boneGroupId. The renderer batches each
    // group into a single instanced draw, so it needs this grouping every frame
    // for both the main and shadow passes. The grouping topology only changes
    // when parts are added/removed, so it is cached and rebuilt lazily instead
    // of rebuilding a std::map per character per pass per frame.
    struct PartGroup {
        int boneGroupId = -1;
        std::vector<int> partIndices;  // indices into parts[]
    };
    const std::vector<PartGroup>& getPartGroups() const {
        if (m_partGroupsDirty || m_partGroupsBuiltSize != parts.size())
            rebuildPartGroups();
        return m_partGroups;
    }

    void setFaction(Faction f) { faction = f; }
    Faction getFaction() const { return faction; }

    virtual void setControlInput(float forward, float turn) {}

    // Health component access. By default each character owns its own
    // HealthComponent. The host can inject an external (non-owning) component
    // via setHealthComponent() so a character SHARES a health store with
    // another system — used to make the player character and the HUD/respawn
    // health a single source of truth (see docs/TurnBasedCombat.md S2). Pass
    // nullptr to revert to the owned component.
    void setHealthComponent(Core::HealthComponent* external) { m_externalHealth = external; }
    Core::HealthComponent* getHealthComponent() override {
        return m_externalHealth ? m_externalHealth : m_health.get();
    }
    const Core::HealthComponent* getHealthComponent() const override {
        return m_externalHealth ? m_externalHealth : m_health.get();
    }

protected:
    // Subclasses must call this after mutating `parts` in a way that does not
    // change its size (e.g. an in-place rebuild). Size changes are detected
    // automatically by getPartGroups().
    void markPartGroupsDirty() { m_partGroupsDirty = true; }

    void rebuildPartGroups() const {
        m_partGroups.clear();
        std::unordered_map<int, size_t> indexByGroup;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!parts[i].useDirectTransform) continue;
            int g = parts[i].boneGroupId;
            auto it = indexByGroup.find(g);
            size_t idx;
            if (it == indexByGroup.end()) {
                idx = m_partGroups.size();
                indexByGroup.emplace(g, idx);
                m_partGroups.push_back(PartGroup{g, {}});
            } else {
                idx = it->second;
            }
            m_partGroups[idx].partIndices.push_back(static_cast<int>(i));
        }
        m_partGroupsDirty = false;
        m_partGroupsBuiltSize = parts.size();
    }

    Physics::PhysicsWorld* physicsWorld;
    std::vector<RagdollPart> parts;
    Faction faction;
    std::unique_ptr<Core::HealthComponent> m_health;
    // Non-owning override; when set, getHealthComponent() returns this instead
    // of the owned m_health (single-source sharing, e.g. player + HUD/respawn).
    Core::HealthComponent* m_externalHealth = nullptr;

    // Lazily-rebuilt cache of `parts` grouped by boneGroupId (see getPartGroups).
    mutable std::vector<PartGroup> m_partGroups;
    mutable bool   m_partGroupsDirty = true;
    mutable size_t m_partGroupsBuiltSize = 0;
};

} // namespace Scene
} // namespace Phyxel
