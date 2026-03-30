#pragma once

#include "scene/Entity.h"
#include "scene/NPCBehavior.h"
#include "scene/CharacterAppearance.h"
#include "graphics/Animation.h"
#include "core/HealthComponent.h"
#include "core/EquipmentSystem.h"
#include "ai/NeedsSystem.h"
#include "ai/WorldView.h"
#include "ui/DialogueData.h"
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Physics { class PhysicsWorld; }
namespace Graphics { class RenderCoordinator; class LightManager; class DayNightCycle; }
namespace Core { class EntityRegistry; class LocationRegistry; }
class ChunkManager;
class RaycastVisualizer;

namespace Scene {

class AnimatedVoxelCharacter;
class VoxelCharacter;
class RagdollCharacter;

/// An NPC entity that wraps an AnimatedVoxelCharacter and delegates
/// behavior to a pluggable NPCBehavior strategy.
class NPCEntity : public Entity {
public:
    NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
              const std::string& name, const std::string& animFile,
              const CharacterAppearance& appearance = CharacterAppearance{});

    /// Construct from in-memory skeleton data (procedural/template-based, no file re-read).
    NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
              const std::string& name, const CharacterAppearance& appearance,
              const Phyxel::Skeleton& skeleton, const Phyxel::VoxelModel& model,
              const std::vector<Phyxel::AnimationClip>& clips);

    /// Construct a physics-driven NPC (active ragdoll via VoxelCharacter).
    NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
              const std::string& name, const std::string& animFile,
              const CharacterAppearance& appearance, bool physicsDriven);

    /// Construct a physics-driven NPC from cached template data.
    NPCEntity(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position,
              const std::string& name, const CharacterAppearance& appearance,
              const Phyxel::Skeleton& skeleton, const Phyxel::VoxelModel& model,
              const std::vector<Phyxel::AnimationClip>& clips, bool physicsDriven);

    ~NPCEntity() override;

    // Entity interface
    void update(float deltaTime) override;
    void render(Graphics::RenderCoordinator* renderer) override;

    // Position delegates to inner character
    void setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;
    void setMoveVelocity(const glm::vec3& velocity) override;

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
                    UI::SpeechBubbleManager* speechBubbleManager, const std::string& entityId,
                    Graphics::DayNightCycle* dayNightCycle = nullptr,
                    Core::LocationRegistry* locationRegistry = nullptr,
                    ChunkManager* chunkManager = nullptr,
                    RaycastVisualizer* raycastVisualizer = nullptr);

    // Attached light (e.g. NPC carrying a lantern)
    void setAttachedLightId(int lightId) { m_attachedLightId = lightId; }
    int getAttachedLightId() const { return m_attachedLightId; }

    // Access inner animated character for animation control
    AnimatedVoxelCharacter* getAnimatedCharacter() const { return m_character.get(); }

    // Access inner physics-driven character (VoxelCharacter)
    VoxelCharacter* getVoxelCharacter() const { return m_voxelCharacter.get(); }

    /// Whether this NPC uses physics-driven (active ragdoll) mode.
    bool isPhysicsDriven() const { return m_voxelCharacter != nullptr; }

    /// Get the underlying RagdollCharacter for rendering (works for both character types).
    RagdollCharacter* getRenderableCharacter() const;

    // Health
    Core::HealthComponent* getHealthComponent() override { return m_health.get(); }
    const Core::HealthComponent* getHealthComponent() const override { return m_health.get(); }

    // Equipment
    Core::EquipmentSlots& getEquipment() { return m_equipment; }
    const Core::EquipmentSlots& getEquipment() const { return m_equipment; }

    // Dialogue
    void setDialogueProvider(std::unique_ptr<UI::DialogueProvider> provider) { m_dialogueProvider = std::move(provider); }
    UI::DialogueProvider* getDialogueProvider() const { return m_dialogueProvider.get(); }

    // Social simulation subsystems (owned per-NPC)
    AI::NeedsSystem& getNeeds() { return m_needs; }
    const AI::NeedsSystem& getNeeds() const { return m_needs; }
    AI::WorldView& getWorldView() { return m_worldView; }
    const AI::WorldView& getWorldView() const { return m_worldView; }

private:
    std::string m_name;
    std::unique_ptr<AnimatedVoxelCharacter> m_character;       // Kinematic (animated) character
    std::unique_ptr<VoxelCharacter> m_voxelCharacter;          // Physics-driven character (active ragdoll)
    std::unique_ptr<NPCBehavior> m_behavior;
    std::unique_ptr<UI::DialogueProvider> m_dialogueProvider;
    NPCContext m_context;
    float m_interactionRadius = 3.0f;
    int m_attachedLightId = -1;
    std::unique_ptr<Core::HealthComponent> m_health = std::make_unique<Core::HealthComponent>(100.0f);
    Core::EquipmentSlots m_equipment;
    AI::NeedsSystem m_needs;
    AI::WorldView m_worldView;
};

} // namespace Scene
} // namespace Phyxel
