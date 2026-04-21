#pragma once

#include "core/InteractionHandler.h"
#include <functional>

namespace Phyxel {
namespace Scene { class NPCEntity; }

namespace Core {

class EntityRegistry;

/// Handles NPC dialogue interactions.
///
/// NPC interactions are special — NPCs are entities, not placed objects.
/// This handler wraps the NPC behavior->onInteract() call and the dialogue callback.
class NPCInteractionHandler : public InteractionHandler {
public:
    using InteractCallback = std::function<void(Scene::NPCEntity* npc)>;

    explicit NPCInteractionHandler(EntityRegistry* registry)
        : m_registry(registry) {}

    const char* getType() const override { return "npc"; }
    float getDefaultRadius() const override { return 3.0f; }
    const char* getDefaultPrompt() const override { return "Interact"; }
    int getPriority() const override { return 50; }

    void setInteractCallback(InteractCallback cb) { m_callback = std::move(cb); }

    void execute(const InteractionContext& ctx) override;

private:
    EntityRegistry* m_registry = nullptr;
    InteractCallback m_callback;
};

} // namespace Core
} // namespace Phyxel
