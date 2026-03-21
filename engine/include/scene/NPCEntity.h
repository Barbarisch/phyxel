#pragma once

#include "scene/Entity.h"
#include "scene/NPCBehavior.h"
#include "ui/DialogueData.h"
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Physics { class PhysicsWorld; }
namespace Graphics { class RenderCoordinator; class LightManager; }
namespace Core { class EntityRegistry; }

namespace Scene {

class AnimatedVoxelCharacter;

/// An NPC entity that wraps an AnimatedVoxelCharacter and delegates
/// behavior to a pluggable NPCBehavior strategy.
class NPCEntity : public Entity {
public:
    NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
              const std::string& name, const std::string& animFile);
    ~NPCEntity() override;

    // Entity interface
    void update(float deltaTime) override;
    void render(Graphics::RenderCoordinator* renderer) override;

    // Position delegates to inner character
    void setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;

    // NPC identity
    const std::string& getName() const { return m_name; }

    // Behavior management
    void setBehavior(std::unique_ptr<NPCBehavior> behavior);
    NPCBehavior* getBehavior() const { return m_behavior.get(); }

    // Interaction
    float getInteractionRadius() const { return m_interactionRadius; }
    void setInteractionRadius(float radius) { m_interactionRadius = radius; }

    // Context wiring (set by NPCManager after construction)
    void setContext(Core::EntityRegistry* registry, Graphics::LightManager* lightManager,
                    UI::SpeechBubbleManager* speechBubbleManager, const std::string& entityId);

    // Attached light (e.g. NPC carrying a lantern)
    void setAttachedLightId(int lightId) { m_attachedLightId = lightId; }
    int getAttachedLightId() const { return m_attachedLightId; }

    // Access inner animated character for animation control
    AnimatedVoxelCharacter* getAnimatedCharacter() const { return m_character.get(); }

    // Dialogue
    void setDialogueProvider(std::unique_ptr<UI::DialogueProvider> provider) { m_dialogueProvider = std::move(provider); }
    UI::DialogueProvider* getDialogueProvider() const { return m_dialogueProvider.get(); }

private:
    std::string m_name;
    std::unique_ptr<AnimatedVoxelCharacter> m_character;
    std::unique_ptr<NPCBehavior> m_behavior;
    std::unique_ptr<UI::DialogueProvider> m_dialogueProvider;
    NPCContext m_context;
    float m_interactionRadius = 3.0f;
    int m_attachedLightId = -1;
};

} // namespace Scene
} // namespace Phyxel
